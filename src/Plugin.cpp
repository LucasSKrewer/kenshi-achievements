// Kenshi Achievements — plugin RE_Kenshi.
// Conta kills e KOs feitos pelos personagens do jogador e libera conquistas
// definidas em achievements.txt (na pasta do mod).
//
// Hooks:
//   Character::declareDead          -> morte (conta 1x por vítima)
//   MedicalSystem::knockout         -> KO (conta na transição consciente -> inconsciente)
//   Character::hitByMeleeAttack /
//   Character::iShotYou             -> lembra o último atacante do jogador (fallback de autoria)
//   FactionManager::saveGameState   -> grava os contadores dentro do save
//   GameWorld::loadAllPlatoons      -> lê os contadores do save
//   SaveManager::newGame            -> zera
//   GameWorld::mainLoop_GPUSensitiveStuff -> UI (MyGUI só pode ser tocado nessa thread)

#include "Lang.h"
#include "SaveFile.h"
#include "Stats.h"

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/CharBody.h>
#include <kenshi/Character.h>
#include <kenshi/Damages.h>
#include <kenshi/Enums.h>
#include <kenshi/Faction.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RaceData.h>
#include <kenshi/SaveInfo.h>
#include <kenshi/SaveManager.h>
#include <kenshi/Tasker.h>
#include <kenshi/gui/ManagementScreen.h>
#include <kenshi/util/hand.h>

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_MultiListBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmsystem.h>

namespace
{
	// Tipo próprio de GameData no save. Faixa livre recomendada pelo KenshiLib: 1100–55000.
	const int SAVE_ITEM_TYPE = 4242;
	const char* const SAVE_SID = "KenshiAchievements_stats";

	const DWORD HIT_MEMORY_MS = 30000;  // quanto tempo um golpe do jogador vale para autoria
	const DWORD TOAST_MS = 6000;
	const char* const VERSION = "1.5.0";
	double gameClock = 0.0; // segundos de jogo (ver tickGameClock)
	DWORD lastReconcile = 0; // ver reconcileCharacters
	bool reconciledOnce = false;
	const DWORD LIMB_HIT_MS = 2000; // membro decepado só conta com golpe do grupo nesse intervalo (#19)
	int panelKey = VK_F6; // @tecla no achievements.txt. F9 é o quickload do Kenshi, F5 o quicksave.

	boost::mutex lock; // hooks de combate podem vir de outras threads

	struct LastHit
	{
		hand attacker;
		hand victim; // pra checar se ela caiu (KO por dano, ver pollDowned)
		DWORD tick;
	};
	std::set<std::string> downNow; // vítimas que já sabemos que estão caídas (não conta o KO de novo)

	// Último agressor de cada personagem do grupo (#20): usado no Memorial quando o jogo não informa
	// quem matou (ex.: sangrou até morrer). Guarda os nomes porque o agressor pode sumir.
	struct SquadHit
	{
		std::string name, faction;
		DWORD tick;
	};
	std::map<std::string, SquadHit> lastHitOnSquad; // chave = handle do personagem do grupo
	const DWORD SQUAD_HIT_MS = 10 * 60 * 1000; // sangrar até morrer pode demorar
	std::map<std::string, LastHit> lastPlayerHit; // chave = handle da vítima
	std::set<std::string> countedDeaths;
	const DWORD KO_DEDUPE_MS = 10000; // knockout() repetido na mesma vítima em menos que isso não conta de novo
	std::map<std::string, DWORD> lastKO; // chave = handle da vítima

	// Quem derrubou cada vítima (#18): se ela morre ainda caída (ex.: esquartejar um animal nocauteado
	// mata na hora e o jogo credita quem esquartejou), a kill é de quem derrubou. Guarda os dados em vez
	// de só o handle, pra contar mesmo que o personagem saia da área.
	struct Downer
	{
		bool player;
		std::string key, name, race, desc;
		double hours; // hora do jogo em que derrubou (vale entre saves), pra descartar os antigos
	};
	std::map<std::string, Downer> downedBy; // chave = handle da vítima
	// Vai pro save (#36): salvar com a vítima caída e carregar perdia quem derrubou, e a morte por
	// sangramento depois não contava. Mais velho que isso a vítima já levantou ou morreu.
	const double DOWNER_KEEP_HOURS = 48.0;
	const char* const P_DOWNER_KEY = "w.k:"; // + handle da vítima
	const char* const P_DOWNER_NAME = "w.n:";
	const char* const P_DOWNER_RACE = "w.r:";
	const char* const P_DOWNER_PLAYER = "w.p:";
	const char* const P_DOWNER_HOUR = "w.h:";

	double gameHours()
	{
		return ou ? ou->getTimeStamp_inGameHours().getTotalHours() : 0.0;
	}

	// ---------- utilitários ----------

	std::string handleKey(RootObjectBase* o)
	{
		return o->getHandle().toString();
	}

	// ID do personagem: o handle, que o log mostrou estável entre save/load (Brooke manteve o mesmo em
	// várias sessões). O InstanceID vem vazio pra personagens. Nunca usar o nome: há homônimos
	// (dois "The Arbiter" de raças diferentes no mesmo grupo).
	std::string charKey(Character* c)
	{
		return handleKey(c);
	}

	// KO sem atacante definido na hora: espera o jogo preencher lastGuyWhoDefeatedMe (#2).
	const DWORD KO_WAIT_MS = 3000;
	struct PendingKO
	{
		hand victim;
		DWORD tick;
	};
	std::vector<PendingKO> pendingKOs;

	void countTakedown(Character* victim, Character* attacker, bool kill, bool stealth = false);

	std::string raceName(Character* c)
	{
		RaceData* r = c->getRace();
		return (r && r->data) ? r->data->name : "";
	}

	std::string factionName(Character* c)
	{
		Faction* f = c->getFaction();
		return f ? f->getName() : "";
	}

	std::string modDir()
	{
		HMODULE hm = NULL;
		char path[MAX_PATH] = { 0 };
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&modDir, &hm);
		GetModuleFileNameA(hm, path, MAX_PATH);
		std::string s(path);
		size_t slash = s.find_last_of("\\/");
		return slash == std::string::npos ? "" : s.substr(0, slash + 1);
	}

	// @debug = 1 no achievements.txt: registra cada etapa no RE_Kenshi_log.txt.
	bool debug = false;
	int debugHits = 0; // limita o spam de golpes

	std::string describe(Character* c)
	{
		if (!c)
			return "(ninguém)";
		std::ostringstream o;
		o << "'" << c->getName() << "' [" << raceName(c) << " / " << factionName(c) << "]"
			<< (c->isPlayerCharacter() ? " (JOGADOR)" : "");
		return o.str();
	}

	void dbg(const std::string& msg)
	{
		if (debug)
			DebugLog("KenshiAchievements[debug]: " + msg);
	}

	// Nocaute/assassinato furtivo não fere a vítima (não passa por addWound nem preenche
	// lastGuyWhoDefeatedMe): procura quem do grupo está executando a tarefa com ela como alvo.
	Character* findStealthAttacker(Character* victim, bool verbose)
	{
		if (!ou || !ou->player)
			return NULL;
		std::string victimKey = handleKey(victim);
		lektor<Character*>& squad = ou->player->playerCharacters;
		for (uint32_t i = 0; i < squad.size(); ++i)
		{
			Character* c = squad[i];
			CharBody* body = c ? c->getBody() : NULL;
			Tasker* task = body ? body->getCurrentAction() : NULL;
			if (!task)
				continue;
			TaskType t = task->key();
			if (t != STEALTH_KNOCKOUT && t != STEALTH_KILL)
				continue;
			bool sameTarget = task->subject.toString() == victimKey;
			if (verbose)
				dbg("  tarefa furtiva de " + describe(c) + (sameTarget ? " -> nessa vítima" : " -> em outro alvo"));
			if (sameTarget)
				return c;
		}
		return NULL;
	}

	// Quem do jogador derrubou a vítima? NULL se não foi o jogador.
	// trustLastGuy = false quando a vítima já estava caída e não sabemos quem a derrubou: aí o
	// lastGuyWhoDefeatedMe pode ser só quem esquartejou/executou (#18).
	bool lastFindWasStealth = false; // o último findPlayerAttacker achou pela tarefa furtiva (#13)

	Character* findPlayerAttacker(Character* victim, bool verbose = true, bool trustLastGuy = true)
	{
		Character* stealth = findStealthAttacker(victim, verbose);
		lastFindWasStealth = stealth != NULL;
		if (stealth)
			return stealth;

		Character* c = trustLastGuy ? victim->lastGuyWhoDefeatedMe.getCharacter() : NULL;
		if (verbose)
			dbg("  lastGuyWhoDefeatedMe = " + describe(c));
		if (c && c->isPlayerCharacter())
			return c;

		std::map<std::string, LastHit>::iterator it = lastPlayerHit.find(handleKey(victim));
		if (it == lastPlayerHit.end())
		{
			if (verbose)
				dbg("  nenhum golpe do jogador lembrado nessa vítima");
			return NULL;
		}
		std::ostringstream age;
		age << (GetTickCount() - it->second.tick) << " ms atrás";
		if (verbose)
			dbg("  último golpe do jogador: " + describe(it->second.attacker.getCharacter()) + ", " + age.str());
		if (GetTickCount() - it->second.tick <= HIT_MEMORY_MS)
		{
			c = it->second.attacker.getCharacter();
			if (c && c->isPlayerCharacter())
				return c;
		}
		return NULL;
	}

	// Chamar com o lock seguro.
	void rememberDowner(Character* victim, Character* downer)
	{
		if (!downer)
		{
			downedBy.erase(handleKey(victim));
			return;
		}
		Downer d;
		d.player = downer->isPlayerCharacter();
		d.key = charKey(downer);
		d.name = downer->getName();
		d.race = raceName(downer);
		d.desc = describe(downer);
		d.hours = gameHours();
		downedBy[handleKey(victim)] = d;
	}

	// Chamar com o lock seguro.
	void exportDowners(std::map<std::string, int>& ints, std::map<std::string, std::string>& strs)
	{
		double now = gameHours();
		for (std::map<std::string, Downer>::const_iterator it = downedBy.begin(); it != downedBy.end(); ++it)
		{
			const Downer& d = it->second;
			if (now - d.hours > DOWNER_KEEP_HOURS)
				continue;
			const std::string& v = it->first;
			ints[P_DOWNER_PLAYER + v] = d.player ? 1 : 0;
			ints[P_DOWNER_HOUR + v] = (int)d.hours;
			strs[P_DOWNER_KEY + v] = d.key;
			strs[P_DOWNER_NAME + v] = d.name;
			if (!d.race.empty())
				strs[P_DOWNER_RACE + v] = d.race;
		}
	}

	bool startsWith(const std::string& s, const char* prefix, std::string& rest)
	{
		size_t n = strlen(prefix);
		if (s.compare(0, n, prefix) != 0)
			return false;
		rest = s.substr(n);
		return true;
	}

	// Chamar com o lock seguro.
	void importDowners(const std::map<std::string, int>& ints, const std::map<std::string, std::string>& strs)
	{
		downedBy.clear();
		for (std::map<std::string, int>::const_iterator it = ints.begin(); it != ints.end(); ++it)
		{
			std::string v;
			if (!startsWith(it->first, P_DOWNER_PLAYER, v))
				continue;
			std::map<std::string, std::string>::const_iterator key = strs.find(P_DOWNER_KEY + v);
			if (key == strs.end())
				continue;
			Downer d;
			d.player = it->second != 0;
			d.key = key->second;
			std::map<std::string, std::string>::const_iterator s = strs.find(P_DOWNER_NAME + v);
			d.name = s != strs.end() ? s->second : "";
			s = strs.find(P_DOWNER_RACE + v);
			d.race = s != strs.end() ? s->second : "";
			std::map<std::string, int>::const_iterator h = ints.find(P_DOWNER_HOUR + v);
			d.hours = h != ints.end() ? h->second : 0.0;
			d.desc = "'" + d.name + "' (do save)";
			downedBy[v] = d;
		}
	}

	// Chamar com o lock seguro.
	void countFor(Character* victim, const std::string& key, const std::string& name, const std::string& race,
		bool kill, bool stealth = false)
	{
		Stats::Takedown t;
		t.key = key;
		t.name = name;
		t.charRace = race;
		t.race = Lang::toEnglish(raceName(victim));
		t.faction = Lang::toEnglish(factionName(victim));
		if (victim->isUnique())
			t.npc = Lang::toEnglish(victim->getName()); // chefes e NPCs únicos (#11)
		t.stealth = stealth;
		t.time = gameClock; // relógio do jogo, pras sequências (#12)
		if (debug)
		{
			std::ostringstream o;
			o << "  registro: t=" << t.time << (t.npc.empty() ? "" : " npc='" + t.npc + "'") << (stealth ? " furtivo" : "");
			dbg(o.str());
		}
		if (kill)
			Stats::recordKill(t);
		else
			Stats::recordKO(t);
	}

	// Memorial (#20): morte de um personagem do grupo.
	void recordSquadDeath(Character* victim)
	{
		boost::lock_guard<boost::mutex> g(lock);
		std::string key = charKey(victim);
		std::string killer, faction;
		Character* by = victim->lastGuyWhoDefeatedMe.getCharacter();
		if (by && !by->isPlayerCharacter())
		{
			killer = by->getName();
			faction = factionName(by);
		}
		else
		{
			std::map<std::string, SquadHit>::iterator it = lastHitOnSquad.find(key);
			if (it != lastHitOnSquad.end() && GetTickCount() - it->second.tick <= SQUAD_HIT_MS)
			{
				killer = it->second.name;
				faction = it->second.faction;
			}
		}
		lastHitOnSquad.erase(key);
		int day = -1;
		if (ou)
			day = (int)ou->getTimeStamp_inGameHours().getTotalDays() + 1;
		std::ostringstream o;
		o << "  BAIXA do grupo: " << describe(victim) << ", morto por '" << killer << "' [" << faction << "], dia " << day;
		dbg(o.str());
		Stats::recordSquadDeath(key, victim->getName(), raceName(victim), killer, faction, day);
	}

	void onTakedown(Character* victim, bool kill, bool wasUnconscious = false)
	{
		if (!victim)
			return;
		dbg(std::string(kill ? "MORTE" : "KO") + " de " + describe(victim));
		if (victim->isPlayerCharacter())
		{
			if (kill)
				recordSquadDeath(victim);
			else
				dbg("  ignorado: vítima é do jogador");
			return;
		}

		boost::lock_guard<boost::mutex> g(lock);

		std::string victimKey = handleKey(victim);
		if (kill && !countedDeaths.insert(victimKey).second)
		{
			dbg("  ignorado: morte dessa vítima já contada");
			return;
		}
		if (kill)
		{
			// #18: morreu ainda caída -> a kill é de quem derrubou, não de quem deu o "golpe final"
			// (esquartejar um animal nocauteado, executar um inimigo caído, sangrar até morrer).
			std::map<std::string, Downer>::iterator dn = downedBy.find(victimKey);
			if (dn != downedBy.end())
			{
				Downer d = dn->second;
				downedBy.erase(dn);
				if (wasUnconscious)
				{
					lastPlayerHit.erase(victimKey);
					if (d.player)
					{
						dbg("  morreu caída: kill vai pra quem derrubou, " + d.desc + " id=" + d.key);
						countFor(victim, d.key, d.name, d.race, true);
					}
					else
					{
						dbg("  morreu caída, derrubada por quem não é do jogador (" + d.desc + "): NÃO contado");
					}
					return;
				}
			}
		}
		if (!kill)
		{
			DWORD now = GetTickCount();
			std::map<std::string, DWORD>::iterator ko = lastKO.find(victimKey);
			if (ko != lastKO.end() && now - ko->second < KO_DEDUPE_MS)
			{
				dbg("  ignorado: KO repetido da mesma vítima");
				return;
			}
			lastKO[victimKey] = now;
			downNow.insert(victimKey); // o pollDowned não conta esse KO de novo
		}
		else
		{
			downNow.erase(victimKey);
		}

		// Morreu caída sem sabermos quem derrubou: o lastGuyWhoDefeatedMe pode ser só quem
		// esquartejou/executou, então não confia nele (#18)
		bool trustLastGuy = !(kill && wasUnconscious);
		if (!trustLastGuy)
			dbg("  estava caída e não se sabe quem derrubou: ignora lastGuyWhoDefeatedMe");
		Character* attacker = findPlayerAttacker(victim, true, trustLastGuy);
		if (!kill)
			rememberDowner(victim, attacker); // NULL: fica pendente; resolvePendingKOs define depois
		if (!attacker)
		{
			if (!kill)
			{
				// O jogo costuma preencher lastGuyWhoDefeatedMe logo depois do knockout(): tenta de novo no mainLoop
				PendingKO p;
				p.victim = victim->getHandle();
				p.tick = GetTickCount();
				pendingKOs.push_back(p);
				dbg("  atacante ainda não definido: esperando até 3 s");
				return;
			}
			dbg("  NÃO contado: atacante do jogador não identificado");
			return;
		}
		countTakedown(victim, attacker, kill, lastFindWasStealth);
		if (kill)
			lastPlayerHit.erase(victimKey);
	}

	// Chamar com o lock seguro.
	void countTakedown(Character* victim, Character* attacker, bool kill, bool stealth)
	{
		std::string key = charKey(attacker);
		dbg("  CONTADO para " + describe(attacker) + " id=" + key);
		countFor(victim, key, attacker->getName(), raceName(attacker), kill, stealth);
	}

	// Chamado a cada frame (thread principal): resolve KOs que ficaram sem atacante.
	void resolvePendingKOs()
	{
		if (pendingKOs.empty())
			return;
		boost::lock_guard<boost::mutex> g(lock);
		DWORD now = GetTickCount();
		for (size_t i = 0; i < pendingKOs.size();)
		{
			Character* victim = pendingKOs[i].victim.getCharacter();
			Character* attacker = victim ? findPlayerAttacker(victim, false) : NULL;
			bool expired = now - pendingKOs[i].tick > KO_WAIT_MS;
			if (attacker || expired || !victim)
			{
				if (attacker)
				{
					std::ostringstream o;
					o << "KO de " << describe(victim) << " resolvido após " << (now - pendingKOs[i].tick) << " ms";
					dbg(o.str());
					countTakedown(victim, attacker, false, lastFindWasStealth);
					rememberDowner(victim, attacker);
				}
				else if (victim)
				{
					// Derrubada por alguém de fora do grupo (ou desconhecido): guarda pra que esquartejar
					// depois não dê a kill pro jogador (#18)
					Character* other = victim->lastGuyWhoDefeatedMe.getCharacter();
					rememberDowner(victim, other);
					dbg("KO de " + describe(victim) + " NÃO contado: ninguém do jogador em 3 s (lastGuyWhoDefeatedMe = "
						+ describe(other) + ")");
				}
				pendingKOs.erase(pendingKOs.begin() + i);
			}
			else
			{
				++i;
			}
		}
	}

	// KO por dano (#18, #2): cair na luta não passa por knockout(), então a cada 250 ms confere se
	// alguma vítima que o grupo acertou recentemente caiu inconsciente. Se caiu, é KO de quem deu o
	// último golpe, e ele fica registrado como quem derrubou. Thread principal.
	DWORD lastPoll = 0;

	// Relógio do jogo pras sequências (#12): tempo real x velocidade do jogo, parado na pausa.
	// (GameWorld::getTimeStamp é tempo real desde a abertura: conta a pausa e ignora o 2x/5x.)
	DWORD lastClockTick = 0;

	void tickGameClock()
	{
		DWORD now = GetTickCount();
		if (lastClockTick && ou && !ou->isPaused())
		{
			double dt = (now - lastClockTick) / 1000.0;
			if (dt > 1.0)
				dt = 1.0; // engasgo ou tela de carregamento não vira tempo de jogo
			gameClock += dt * ou->getFrameSpeedMultiplier();
		}
		lastClockTick = now;
	}

	void pollDowned()
	{
		DWORD now = GetTickCount();
		if (now - lastPoll < 250 || lastPlayerHit.empty())
			return;
		lastPoll = now;
		boost::lock_guard<boost::mutex> g(lock);
		for (std::map<std::string, LastHit>::iterator it = lastPlayerHit.begin(); it != lastPlayerHit.end(); ++it)
		{
			Character* victim = it->second.victim.getCharacter();
			if (!victim)
				continue;
			bool down = victim->medical.unconcious && !victim->medical.dead;
			if (!down)
			{
				downNow.erase(it->first);
				continue;
			}
			if (!downNow.insert(it->first).second)
				continue; // já sabíamos que caiu (KO contado aqui ou pelo knockout())
			std::map<std::string, DWORD>::iterator ko = lastKO.find(it->first);
			if (ko != lastKO.end() && now - ko->second < KO_DEDUPE_MS)
				continue; // o knockout() acabou de contar esse
			Character* attacker = it->second.attacker.getCharacter();
			if (!attacker || !attacker->isPlayerCharacter())
				continue;
			lastKO[it->first] = now;
			std::ostringstream o;
			o << "KO por dano de " << describe(victim) << " (" << (now - it->second.tick) << " ms após o último golpe)";
			dbg(o.str());
			countTakedown(victim, attacker, false);
			rememberDowner(victim, attacker);
		}
	}

	void rememberHit(Character* victim, Character* attacker)
	{
		if (victim && attacker && victim->isPlayerCharacter() && !attacker->isPlayerCharacter())
		{
			boost::lock_guard<boost::mutex> g(lock);
			SquadHit h;
			h.name = attacker->getName();
			h.faction = factionName(attacker);
			h.tick = GetTickCount();
			lastHitOnSquad[handleKey(victim)] = h;
			return;
		}
		if (!victim || !attacker || !attacker->isPlayerCharacter() || victim->isPlayerCharacter())
			return;
		if (debug && debugHits < 60)
		{
			++debugHits;
			dbg("golpe do jogador: " + describe(attacker) + " -> " + describe(victim));
		}
		boost::lock_guard<boost::mutex> g(lock);
		LastHit h;
		h.attacker = attacker->getHandle();
		h.victim = victim->getHandle();
		h.tick = GetTickCount();
		std::string key = handleKey(victim);
		lastPlayerHit[key] = h;
		if (!victim->medical.unconcious)
			downNow.erase(key); // está de pé apanhando: um KO futuro conta de novo
	}

	void resetAll()
	{
		boost::lock_guard<boost::mutex> g(lock);
		Stats::reset();
		lastPlayerHit.clear();
		countedDeaths.clear();
		lastKO.clear();
		pendingKOs.clear();
		downedBy.clear();
		downNow.clear();
		lastHitOnSquad.clear();
		// Confere 10 s depois de carregar: no 1º frame o grupo ainda não acha ninguém pelo handle
		// (o log mostrava "0 com handle válido, 35 sem dono no grupo" e a conferência não servia pra nada)
		lastReconcile = GetTickCount();
		reconciledOnce = false;
	}

	// ---------- hooks de combate ----------

	void (*declareDead_orig)(Character*) = NULL;
	void declareDead_hook(Character* self)
	{
		bool wasUnconscious = self->medical.unconcious; // antes: a morte pode limpar o estado
		declareDead_orig(self);
		dbg(std::string("declareDead() chamado") + (wasUnconscious ? " (vítima estava caída)" : ""));
		onTakedown(self, true, wasUnconscious);
	}

	void (*knockout_orig)(MedicalSystem*, float) = NULL;
	void knockout_hook(MedicalSystem* self, float skill01)
	{
		bool wasDown = self->unconcious || self->dead;
		knockout_orig(self, skill01);
		if (debug)
		{
			std::ostringstream o;
			o << "knockout() em " << describe(self->me) << ": antes unconcious/dead=" << wasDown
				<< ", depois unconcious=" << self->unconcious << " dead=" << self->dead;
			dbg(o.str());
		}
		// O jogo só marca unconcious depois (fora desta chamada), então a chamada em si é o KO (#2).
		if (!wasDown && !self->dead)
			onTakedown(self->me, false);
	}

	HitMaterialType (*hitByMelee_orig)(Character*, CutDirection, Damages&, Character*, CombatTechniqueData*, int) = NULL;
	HitMaterialType hitByMelee_hook(Character* self, CutDirection dir, Damages& damage, Character* who, CombatTechniqueData* attack, int comboID)
	{
		rememberHit(self, who);
		return hitByMelee_orig(self, dir, damage, who, attack, comboID);
	}

	// Todo ferimento (melee, projétil, animal) passa por addWound com o atacante: fonte mais
	// confiável de "quem bateu" do que hitByMeleeAttack/iShotYou.
	GameData* (*addWound_orig)(MedicalSystem*, bool, CutDirection, Damages&, int&, RootObject*, AttackDirection::Enum&, Harpoon*) = NULL;
	GameData* addWound_hook(MedicalSystem* self, bool lowBlow, CutDirection area, Damages& damage, int& material,
		RootObject* attacker, AttackDirection::Enum& attackDirection, Harpoon* harpoon)
	{
		if (attacker && self->me)
			rememberHit(self->me, attacker->getHandle().getCharacter());
		return addWound_orig(self, lowBlow, area, damage, material, attacker, attackDirection, harpoon);
	}

	// Membro decepado (#19): conta pra quem do grupo acertou a vítima instantes antes. A janela curta
	// exclui o que não é combate do jogador: membros restaurados ao carregar o save, comidos por
	// animais, arrancados por máquinas.
	void (*amputate_orig)(MedicalSystem*, RobotLimbs::Limb, bool, const Ogre::Vector3&) = NULL;
	void amputate_hook(MedicalSystem* self, RobotLimbs::Limb limb, bool createSeveredItem, const Ogre::Vector3& force)
	{
		// O jogo chama amputate a cada frame pra certas criaturas (Skimmers, aranhas...) sem mudar nada:
		// só vale quando o membro realmente vira cotoco.
		LimbState before = self->getLimbState(limb);
		amputate_orig(self, limb, createSeveredItem, force);
		if (before == LIMB_STUMP || self->getLimbState(limb) != LIMB_STUMP)
			return;

		Character* victim = self->me;
		if (!victim || victim->isPlayerCharacter())
			return;
		boost::lock_guard<boost::mutex> g(lock);
		std::map<std::string, LastHit>::iterator it = lastPlayerHit.find(handleKey(victim));
		Character* attacker = NULL;
		if (it != lastPlayerHit.end() && GetTickCount() - it->second.tick <= LIMB_HIT_MS)
			attacker = it->second.attacker.getCharacter();
		if (attacker && attacker->isPlayerCharacter())
		{
			dbg("MEMBRO decepado de " + describe(victim) + " por " + describe(attacker));
			Stats::recordLimb(charKey(attacker), attacker->getName(), raceName(attacker));
		}
		else
		{
			dbg("membro perdido por " + describe(victim) + " sem golpe recente do jogador: não contado");
		}
	}

	bool (*iShotYou_orig)(Character*, Character*, Harpoon*, bool) = NULL;
	bool iShotYou_hook(Character* self, Character* attacker, Harpoon* poon, bool onPurpose)
	{
		rememberHit(self, attacker);
		return iShotYou_orig(self, attacker, poon, onPurpose);
	}

	// ---------- save / load ----------

	void (*saveGameState_orig)(FactionManager*, GameDataContainer*) = NULL;
	void saveGameState_hook(FactionManager* self, GameDataContainer* container)
	{
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		{
			boost::lock_guard<boost::mutex> g(lock);
			Stats::exportTo(ints, strs);
			exportDowners(ints, strs);
		}

		GameData* d = container->createNewData((itemType)SAVE_ITEM_TYPE, SAVE_SID, "Kenshi Achievements");
		if (d)
		{
			d->idata.clear();
			d->sdata.clear();
			for (std::map<std::string, int>::const_iterator it = ints.begin(); it != ints.end(); ++it)
				d->idata[it->first] = it->second;
			for (std::map<std::string, std::string>::const_iterator it = strs.begin(); it != strs.end(); ++it)
				d->sdata[it->first] = it->second;
		}
		else
		{
			ErrorLog("KenshiAchievements: createNewData falhou, contadores não foram salvos");
		}

		saveGameState_orig(self, container);
	}

	// Import (#37): o Kenshi cria um mundo novo e traz personagens/construções do save escolhido, mas não o
	// registro do mod. Guardamos o quick.save importado e lemos os contadores direto dele. Fica valendo até
	// o jogo rodar (o import pode passar por newGame e por loadAllPlatoons, e os dois zeram tudo antes).
	std::string pendingImport;
	DWORD pendingImportTick = 0;
	const DWORD IMPORT_VALID_MS = 10 * 60 * 1000; // o mundo novo do import pode demorar a gerar

	bool loadFromImportedSave()
	{
		if (pendingImport.empty())
			return false;
		if (GetTickCount() - pendingImportTick > IMPORT_VALID_MS)
		{
			pendingImport.clear(); // velho: não é o carregamento desse import
			return false;
		}
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		std::string error;
		if (!SaveFile::readRecord(pendingImport, SAVE_ITEM_TYPE, SAVE_SID, ints, strs, error))
		{
			DebugLog("KenshiAchievements: import sem contadores (" + error + ")");
			return false;
		}
		boost::lock_guard<boost::mutex> g(lock);
		Stats::importFrom(ints, strs);
		// Não traz quem derrubou cada vítima: no mundo novo esses NPCs são outros. Os personagens ganham
		// handle novo; a conferência (reconcileCharacters) junta cada entrada com quem tem nome e raça iguais.
		lastReconcile = GetTickCount();
		reconciledOnce = false;
		DebugLog("KenshiAchievements: contadores trazidos do save importado " + pendingImport);
		return true;
	}

	void (*import_orig)(SaveManager*, const SaveInfo&, int) = NULL;
	void import_hook(SaveManager* self, const SaveInfo& s, int flags)
	{
		// location/name variam entre versões; fica com o primeiro caminho que existir
		std::string base = s.location;
		if (!base.empty() && base[base.size() - 1] != '\\' && base[base.size() - 1] != '/')
			base += "\\";
		std::string candidates[] = {
			base + s.name + "\\quick.save",
			self->getSavePath() + s.name + "\\quick.save",
			self->getSavePath() + "\\" + s.name + "\\quick.save",
		};
		pendingImport.clear();
		pendingImportTick = GetTickCount();
		for (int i = 0; i < 3; ++i)
		{
			if (GetFileAttributesA(candidates[i].c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				pendingImport = candidates[i];
				break;
			}
		}
		std::ostringstream o;
		o << "KenshiAchievements: import de '" << s.name << "' (location '" << s.location << "', flags " << flags << ") -> "
			<< (pendingImport.empty() ? "quick.save não encontrado" : pendingImport);
		DebugLog(o.str());
		import_orig(self, s, flags);
	}

	void (*loadAllPlatoons_orig)(GameWorld*) = NULL;
	void loadAllPlatoons_hook(GameWorld* self)
	{
		resetAll();
		loadAllPlatoons_orig(self);

		auto it = ou->savedata.gamedataSID.find(SAVE_SID);
		if (it == ou->savedata.gamedataSID.end() || !it->second)
		{
			if (!loadFromImportedSave())
				DebugLog("KenshiAchievements: save sem contadores (save novo ou anterior ao mod)");
			return;
		}
		pendingImport.clear(); // carregamento normal de um save com contadores: não é import

		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		for (auto i = it->second->idata.begin(); i != it->second->idata.end(); ++i)
			ints[i->first] = i->second;
		for (auto s = it->second->sdata.begin(); s != it->second->sdata.end(); ++s)
			strs[s->first] = s->second;

		boost::lock_guard<boost::mutex> g(lock);
		Stats::importFrom(ints, strs);
		importDowners(ints, strs);
		DebugLog("KenshiAchievements: contadores carregados do save");
		if (debug)
		{
			std::ostringstream o;
			o << downedBy.size() << " vítima(s) caída(s) com quem derrubou, vindas do save";
			dbg(o.str());
		}
	}

	void (*newGame_orig)(SaveManager*, const std::string&) = NULL;
	void newGame_hook(SaveManager* self, const std::string& startId)
	{
		resetAll();
		newGame_orig(self, startId);
		loadFromImportedSave(); // import que passa pelo newGame
	}

	// ---------- UI (thread principal) ----------

	struct Toast
	{
		std::string title;
		std::string body;
	};
	std::deque<Toast> toastQueue;
	MyGUI::Window* toastWindow = NULL;
	DWORD toastShownAt = 0;

	MyGUI::Window* panel = NULL;
	MyGUI::EditBox* panelText = NULL;
	MyGUI::Button* tabStats = NULL;
	MyGUI::Button* tabAchievements = NULL;
	MyGUI::Button* tabMemorial = NULL;
	MyGUI::Button* tabVictims = NULL;
	// Tabela por personagem na aba Estatísticas (#22): a fonte do Kenshi não é monoespaçada, então
	// alinhar com espaços não funciona; é a mesma tabela das telas de salvar/carregar.
	MyGUI::MultiListBox* charTable = NULL;
	std::string tableSignature; // só refaz as linhas quando mudam (refazer perde a ordenação escolhida)
	// Aba Vítimas (#34): raças e facções em tabelas; do grupo todo ou do personagem selecionado (#28)
	MyGUI::Button* victimsSquadBtn = NULL;
	MyGUI::Button* victimsSelBtn = NULL;
	MyGUI::MultiListBox* raceTable = NULL;
	MyGUI::MultiListBox* factionTable = NULL;
	std::string raceSignature, factionSignature;
	bool victimsOfSelected = false; // lembrado entre aberturas
	std::string victimsKey; // personagem da aba Vítimas (vazio = grupo); definido em buildReport
	MyGUI::Button* exportBtn = NULL; // #27
	bool exportRequested = false;
	const int TAB_STATS = 0, TAB_ACHIEVEMENTS = 1, TAB_MEMORIAL = 2, TAB_VICTIMS = 3;
	int currentTab = TAB_STATS; // lembrado entre aberturas
	std::string panelLastText; // só reescreve o texto quando muda (setCaption volta a rolagem pro topo)
	bool panelCloseRequested = false;
	DWORD panelRefreshedAt = 0;

	void onPanelButton(MyGUI::Window* sender, const std::string& name)
	{
		// Não destrói aqui: estamos dentro do evento do próprio widget. Fecha no próximo frame.
		if (name == "close")
			panelCloseRequested = true;
	}

	// Estatísticas: texto em cima e tabela embaixo. Conquistas/Memorial: só o texto, na altura toda.
	// Vítimas: botões grupo/selecionado, uma linha de título e as duas tabelas. Embaixo de tudo, Exportar.
	void layoutForTab()
	{
		if (!panelText)
			return;
		bool stats = currentTab == TAB_STATS && charTable;
		bool victims = currentTab == TAB_VICTIMS;
		if (charTable)
			charTable->setVisible(currentTab == TAB_STATS);
		if (victimsSquadBtn)
			victimsSquadBtn->setVisible(victims);
		if (victimsSelBtn)
			victimsSelBtn->setVisible(victims);
		if (raceTable)
			raceTable->setVisible(victims);
		if (factionTable)
			factionTable->setVisible(victims);
		// Na aba Vítimas a caixa é só uma linha de título: sem barra de rolagem
		panelText->setVisibleVScroll(!victims);
		if (victims)
			panelText->setRealCoord(0.02f, 0.17f, 0.96f, 0.09f);
		else
			panelText->setRealCoord(0.02f, 0.10f, 0.96f, stats ? 0.36f : 0.80f);
		panelLastText.clear();
		tableSignature.clear();
		raceSignature.clear();
		factionSignature.clear();
	}

	// Clique no cabeçalho ordena; nas colunas de número, por valor ("100" depois de "20").
	void tableLess(MyGUI::MultiListBox* sender, size_t column, const MyGUI::UString& a, const MyGUI::UString& b, bool& less)
	{
		if (column == 0)
			less = a.asUTF8() < b.asUTF8();
		else
			less = atoi(a.asUTF8_c_str()) < atoi(b.asUTF8_c_str());
	}

	std::string itos(int v)
	{
		std::ostringstream o;
		o << v;
		return o.str();
	}

	void fillVictimTable(MyGUI::MultiListBox* table, std::string& signature, const std::vector<Stats::VictimRow>& rows)
	{
		if (!table)
			return;
		// Começa com algo fixo: lista vazia não pode dar a mesma assinatura da tabela recém-limpa (""),
		// senão trocar pra um personagem sem vítimas deixava as linhas do grupo na tela
		std::ostringstream sig;
		sig << "rows:" << rows.size() << '\n';
		for (size_t i = 0; i < rows.size(); ++i)
			sig << rows[i].name << '|' << rows[i].kills << '|' << rows[i].kos << '\n';
		if (sig.str() == signature)
			return;
		signature = sig.str();
		table->removeAllItems();
		for (size_t i = 0; i < rows.size(); ++i)
		{
			table->addItem(rows[i].name);
			table->setSubItemNameAt(1, i, itos(rows[i].kills));
			table->setSubItemNameAt(2, i, itos(rows[i].kos));
		}
	}

	void fillTable(const std::vector<Stats::CharRow>& rows)
	{
		std::ostringstream sig;
		sig << "rows:" << rows.size() << '\n'; // ver fillVictimTable
		for (size_t i = 0; i < rows.size(); ++i)
			sig << rows[i].name << '|' << rows[i].kills << '|' << rows[i].kos << '|' << rows[i].stealthKos << '|' << rows[i].limbs << '\n';
		if (sig.str() == tableSignature)
			return;
		tableSignature = sig.str();
		charTable->removeAllItems();
		for (size_t i = 0; i < rows.size(); ++i)
		{
			charTable->addItem(rows[i].name);
			charTable->setSubItemNameAt(1, i, itos(rows[i].kills));
			charTable->setSubItemNameAt(2, i, itos(rows[i].kos));
			charTable->setSubItemNameAt(3, i, itos(rows[i].stealthKos));
			charTable->setSubItemNameAt(4, i, itos(rows[i].limbs));
		}
	}

	void selectTab(int tab)
	{
		currentTab = tab;
		if (tabStats)
			tabStats->setStateSelected(tab == 0);
		if (tabAchievements)
			tabAchievements->setStateSelected(tab == 1);
		if (tabMemorial)
			tabMemorial->setStateSelected(tab == 2);
		if (tabVictims)
			tabVictims->setStateSelected(tab == TAB_VICTIMS);
		layoutForTab();
		panelRefreshedAt = 0; // força refresh no próximo frame
	}

	void onTabClick(MyGUI::Widget* sender)
	{
		if (sender == tabAchievements)
			selectTab(TAB_ACHIEVEMENTS);
		else if (sender == tabMemorial)
			selectTab(TAB_MEMORIAL);
		else if (sender == tabVictims)
			selectTab(TAB_VICTIMS);
		else
			selectTab(TAB_STATS);
	}

	void onVictimsModeClick(MyGUI::Widget* sender)
	{
		victimsOfSelected = sender == victimsSelBtn;
		selectTab(TAB_VICTIMS);
	}

	void onExportClick(MyGUI::Widget* sender)
	{
		exportRequested = true; // grava no próximo frame, fora do evento do botão
	}

	void selectedCharacter(std::string& key, std::string& name)
	{
		Character* sel = (ou && ou->player) ? ou->player->selectedCharacter.getCharacter() : NULL;
		if (sel)
		{
			key = charKey(sel);
			name = sel->getName();
		}
	}

	// Texto da aba atual. Chamar na thread principal (lê o personagem selecionado).
	std::string buildReport()
	{
		if (currentTab == 1)
		{
			boost::lock_guard<boost::mutex> g(lock);
			return Stats::achievementsReport();
		}
		if (currentTab == 2)
		{
			boost::lock_guard<boost::mutex> g(lock);
			return Stats::memorialReport();
		}
		std::string selKey, selName;
		selectedCharacter(selKey, selName);
		if (currentTab == TAB_VICTIMS)
		{
			if (victimsSelBtn)
				victimsSelBtn->setCaption(selName.empty() ? Lang::tr("victims.selected_none", "Selected character")
					: Lang::fill(Lang::tr("victims.selected", "Selected: {name}"), "name", selName));
			if (victimsSquadBtn)
			{
				victimsSquadBtn->setStateSelected(!victimsOfSelected);
				victimsSelBtn->setStateSelected(victimsOfSelected);
			}
			if (!victimsOfSelected)
			{
				victimsKey.clear();
				return Lang::tr("victims.title_squad", "Victims of the whole squad (kills / KOs)");
			}
			victimsKey = selKey;
			if (selName.empty())
			{
				victimsKey = "-"; // ninguém selecionado: tabelas vazias, não as do grupo
				return Lang::tr("stats.no_selection", "(no character selected)");
			}
			std::string title = Lang::fill(Lang::tr("victims.title_char", "Victims of {name} (kills / KOs)"), "name", selName);
			boost::lock_guard<boost::mutex> g(lock);
			if (Stats::victimRows(false, selKey).empty())
				title += "  -  " + Lang::tr("victims.since", "counted from v1.5.0 on");
			return title;
		}
		boost::lock_guard<boost::mutex> g(lock);
		return Stats::statsReport(selKey, selName, charTable == NULL); // com tabela, a lista sai do texto
	}

	void log(const std::string& owner, const std::string& msg);

	// #27: grava exports/stats-<data>.txt e .csv na pasta do mod e avisa no log de mensagens.
	void exportStats()
	{
		std::string selKey, selName;
		selectedCharacter(selKey, selName);
		std::string text, table;
		{
			boost::lock_guard<boost::mutex> g(lock);
			text = Stats::exportText(selKey, selName);
			table = Stats::exportCsv();
		}
		std::string dir = modDir() + "exports\\";
		CreateDirectoryA(dir.c_str(), NULL);
		SYSTEMTIME t;
		GetLocalTime(&t);
		char stamp[32];
		sprintf_s(stamp, "%04d-%02d-%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		std::string base = dir + "stats-" + stamp;
		std::ofstream txt((base + ".txt").c_str(), std::ios::binary);
		txt << "\xEF\xBB\xBF" << text; // BOM: o Bloco de Notas e o Excel acertam a acentuação
		std::ofstream csvFile((base + ".csv").c_str(), std::ios::binary);
		csvFile << "\xEF\xBB\xBF" << table;
		bool ok = txt.good() && csvFile.good();
		std::string msg = Lang::fill(Lang::tr(ok ? "export.done" : "export.failed",
			ok ? "Statistics exported to {path}" : "Could not write {path}"), "path", base + ".txt / .csv");
		log(Lang::tr("panel.title", "Kills & Achievements"), msg);
		DebugLog("KenshiAchievements: " + msg);
	}
	bool keyWasDown = false;

	// Som da conquista (@som no achievements.txt):
	//   arquivo .wav na pasta do mod -> tocado pelo Windows (padrão: achievement.wav)
	//   nome de evento Wwise do Kenshi (ex.: Change_Level) -> tocado pelo motor de áudio do jogo
	//   Evento:Estado (ex.: Notifications:Building_Complete) -> antes seta o switch de mesmo nome do evento
	//   "nenhum" -> mudo
	std::vector<char> soundWav;
	std::string soundEvent;
	std::string soundSwitch;

	void loadSound(const std::string& dir)
	{
		std::string s = Stats::setting("sound", Stats::setting("som", "Notifications:Building_Complete"));
		if (s == "nenhum" || s == "none")
			return;
		if (s.size() > 4 && _stricmp(s.c_str() + s.size() - 4, ".wav") == 0)
		{
			std::ifstream f((dir + s).c_str(), std::ios::binary);
			if (f)
				soundWav.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			if (soundWav.empty())
				ErrorLog("KenshiAchievements: não abriu o som " + dir + s);
			return;
		}
		size_t colon = s.find(':');
		soundEvent = s.substr(0, colon);
		if (colon != std::string::npos)
			soundSwitch = s.substr(colon + 1);
	}

	void playUnlockSound()
	{
		if (!soundWav.empty())
		{
			PlaySoundA(&soundWav[0], NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
			return;
		}
		if (soundEvent.empty() || !ou || !ou->player)
			return;
		// Evento Wwise precisa de um objeto de áudio: usa o personagem selecionado (ou o 1º do grupo).
		Character* c = ou->player->selectedCharacter.getCharacter();
		if (!c && ou->player->playerCharacters.size() > 0)
			c = ou->player->playerCharacters[0];
		if (!c)
			return;
		if (!soundSwitch.empty())
			c->audioValue(soundEvent.c_str(), soundSwitch.c_str());
		c->audioEvent(soundEvent.c_str(), SOUNDRANGE_ALWAYS);
	}

	void log(const std::string& owner, const std::string& msg)
	{
		ManagementScreen* ms = ManagementScreen::getSingleton();
		if (ms)
			ms->addMessage(owner, msg, ML_SYSTEM);
	}

	void showNextToast(MyGUI::Gui* gui)
	{
		if (toastWindow && GetTickCount() - toastShownAt >= TOAST_MS)
		{
			gui->destroyWidget(toastWindow);
			toastWindow = NULL;
		}
		if (toastWindow || toastQueue.empty())
			return;

		Toast t = toastQueue.front();
		toastQueue.pop_front();

		toastWindow = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.35f, 0.04f, 0.30f, 0.12f,
			MyGUI::Align::Default, "Overlapped", "KenshiAchievementsToast");
		toastWindow->setCaption(Lang::tr("toast.title", "Achievement unlocked!"));
		MyGUI::TextBox* text = toastWindow->getClientWidget()->createWidgetReal<MyGUI::TextBox>(
			"Kenshi_TextboxStandardText", 0.03f, 0.05f, 0.94f, 0.9f, MyGUI::Align::Stretch);
		text->setTextAlign(MyGUI::Align::Center);
		text->setCaption(t.title + "\n" + t.body);
		toastShownAt = GetTickCount();
	}

	void togglePanel(MyGUI::Gui* gui)
	{
		if (panel)
		{
			gui->destroyWidget(panel);
			panel = NULL;
			panelText = NULL;
			tabStats = NULL;
			tabAchievements = NULL;
			tabMemorial = NULL;
			tabVictims = NULL;
			charTable = NULL;
			victimsSquadBtn = NULL;
			victimsSelBtn = NULL;
			raceTable = NULL;
			factionTable = NULL;
			exportBtn = NULL;
			return;
		}
		panel = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.30f, 0.15f, 0.40f, 0.65f,
			MyGUI::Align::Default, "Overlapped", "KenshiAchievementsPanel");
		panel->setCaption(Lang::tr("panel.title", "Kills & Achievements") + "  v" + VERSION);
		MyGUI::Widget* client = panel->getClientWidget();

		// Abas: quatro botões no topo, o da aba atual fica "selecionado"
		MyGUI::Button** tabs[] = { &tabStats, &tabAchievements, &tabMemorial, &tabVictims };
		const char* tabKeys[] = { "tab.stats", "tab.achievements", "tab.memorial", "tab.victims" };
		const char* tabDefaults[] = { "Statistics", "Achievements", "Memorial", "Victims" };
		for (int i = 0; i < 4; ++i)
		{
			MyGUI::Button* b = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.02f + i * 0.2425f, 0.01f,
				0.23f, 0.07f, MyGUI::Align::Top | MyGUI::Align::HStretch);
			b->setCaption(Lang::tr(tabKeys[i], tabDefaults[i]));
			b->eventMouseButtonClick += MyGUI::newDelegate(onTabClick);
			*tabs[i] = b;
		}

		// Aba Vítimas: grupo todo / personagem selecionado
		victimsSquadBtn = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.02f, 0.10f, 0.47f, 0.06f,
			MyGUI::Align::Top | MyGUI::Align::HStretch);
		victimsSquadBtn->setCaption(Lang::tr("victims.squad", "Whole squad"));
		victimsSquadBtn->eventMouseButtonClick += MyGUI::newDelegate(onVictimsModeClick);
		victimsSelBtn = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.51f, 0.10f, 0.47f, 0.06f,
			MyGUI::Align::Top | MyGUI::Align::HStretch);
		victimsSelBtn->setCaption(Lang::tr("victims.selected_none", "Selected character"));
		victimsSelBtn->eventMouseButtonClick += MyGUI::newDelegate(onVictimsModeClick);

		exportBtn = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.66f, 0.92f, 0.32f, 0.06f,
			MyGUI::Align::Bottom | MyGUI::Align::Right);
		exportBtn->setCaption(Lang::tr("export.button", "Export"));
		exportBtn->eventMouseButtonClick += MyGUI::newDelegate(onExportClick);

		// Mesma skin/propriedades do log de mensagens do jogo (MessagesTextBox em Kenshi_OverviewWindow.layout):
		// multilinha, texto no topo e barra de rolagem. A Kenshi_EditBox é de uma linha só (mostrava só a 1ª linha).
		panelText = client->createWidgetReal<MyGUI::EditBox>(
			"Kenshi_WordWrap", 0.02f, 0.10f, 0.96f, 0.88f, MyGUI::Align::Stretch);
		panelText->setEditMultiLine(true);
		panelText->setEditStatic(true);
		panelText->setEditReadOnly(true);
		panelText->setEditWordWrap(true);
		panelText->setVisibleVScroll(true);
		panelText->setVisibleHScroll(false);
		// A EditBox do MyGUI corta em 2048 caracteres por padrão; com 41 conquistas a aba já passa disso
		panelText->setMaxTextLength(1000000);
		panelLastText.clear();

		charTable = client->createWidgetReal<MyGUI::MultiListBox>("Kenshi_MultiListBox", 0.02f, 0.47f, 0.96f, 0.43f,
			MyGUI::Align::Stretch);
		int width = charTable->getWidth() - 30; // desconta a barra de rolagem
		int number = width * 13 / 100;
		charTable->addColumn(Lang::tr("table.name", "Character"), width - 4 * number);
		charTable->addColumn(Lang::tr("table.kills", "Kills"), number);
		charTable->addColumn(Lang::tr("table.kos", "KOs"), number);
		charTable->addColumn(Lang::tr("table.stealth", "Stealth"), number);
		charTable->addColumn(Lang::tr("table.limbs", "Limbs"), number);
		charTable->requestOperatorLess = MyGUI::newDelegate(tableLess);
		tableSignature.clear();

		MyGUI::MultiListBox** victimTables[] = { &raceTable, &factionTable };
		const char* firstColKey[] = { "table.race", "table.faction" };
		const char* firstColDefault[] = { "Race", "Faction" };
		for (int i = 0; i < 2; ++i)
		{
			MyGUI::MultiListBox* t = client->createWidgetReal<MyGUI::MultiListBox>("Kenshi_MultiListBox", 0.02f,
				i == 0 ? 0.27f : 0.59f, 0.96f, 0.31f, MyGUI::Align::Stretch);
			int w = t->getWidth() - 30;
			int num = w * 18 / 100;
			t->addColumn(Lang::tr(firstColKey[i], firstColDefault[i]), w - 2 * num);
			t->addColumn(Lang::tr("table.kills", "Kills"), num);
			t->addColumn(Lang::tr("table.kos", "KOs"), num);
			t->requestOperatorLess = MyGUI::newDelegate(tableLess);
			*victimTables[i] = t;
		}
		raceSignature.clear();
		factionSignature.clear();

		panel->eventWindowButtonPressed += MyGUI::newDelegate(onPanelButton);
		selectTab(currentTab);
	}

	// "F6", "F10", "L", "7", "NUM5" -> virtual-key. 0 se não reconhecer.
	int parseKey(const std::string& raw)
	{
		std::string s;
		for (size_t i = 0; i < raw.size(); ++i)
			s += (char)toupper((unsigned char)raw[i]);
		if (s.size() >= 2 && s[0] == 'F')
		{
			int n = atoi(s.c_str() + 1);
			if (n >= 1 && n <= 12)
				return VK_F1 + n - 1;
		}
		if (s.size() == 4 && s.compare(0, 3, "NUM") == 0 && isdigit((unsigned char)s[3]))
			return VK_NUMPAD0 + (s[3] - '0');
		if (s.size() == 1 && (isalpha((unsigned char)s[0]) || isdigit((unsigned char)s[0])))
			return s[0];
		return 0;
	}

	bool gameHasFocus()
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		return pid == GetCurrentProcessId();
	}

	bool modifierHeld()
	{
		return (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000)
			|| (GetAsyncKeyState(VK_MENU) & 0x8000);
	}

	// Não abre o painel enquanto o jogador digita (renomear personagem, etc.).
	bool typingInTextBox()
	{
		MyGUI::InputManager* input = MyGUI::InputManager::getInstancePtr();
		MyGUI::Widget* w = input ? input->getKeyFocusWidget() : NULL;
		return w && w->castType<MyGUI::EditBox>(false) && w != panelText;
	}

	// Uma vez, com o mundo carregado: quais raças/facções existem nos dados (jogo + mods ativos).
	// Conquistas de mods ausentes ficam ocultas (#7). Os dados não mudam entre saves.
	bool knownNamesChecked = false;

	void checkKnownNames()
	{
		if (knownNamesChecked || !ou)
			return;
		knownNamesChecked = true;
		std::set<std::string> races, factions, npcs;
		for (auto it = ou->gamedata.gamedataSID.begin(); it != ou->gamedata.gamedataSID.end(); ++it)
		{
			GameData* d = it->second;
			if (!d)
				continue;
			if (d->type == RACE)
				races.insert(Lang::toEnglish(d->name));
			else if (d->type == FACTION)
				factions.insert(Lang::toEnglish(d->name));
			else if (d->type == CHARACTER)
				npcs.insert(Lang::toEnglish(d->name)); // pra ocultar npc_* de chefes que não existem (#11)
		}
		std::vector<std::string> hidden;
		{
			boost::lock_guard<boost::mutex> g(lock);
			hidden = Stats::setKnownNames(races, factions, npcs);
		}
		std::ostringstream o;
		o << "KenshiAchievements: " << races.size() << " races, " << factions.size() << " factions, " << npcs.size() << " characters in game data; "
			<< hidden.size() << " achievement(s) hidden (content not installed)";
		for (size_t i = 0; i < hidden.size(); ++i)
			o << (i ? ", " : ": ") << hidden[i];
		DebugLog(o.str());
	}

	// O handle de um personagem muda quando ele troca de esquadrão, e o mesmo personagem passava a contar
	// numa entrada nova (#22). A cada 10 s: entrada cujo handle não aponta mais pra um personagem vivo do
	// grupo com aquele nome é somada no handle atual de quem tem o mesmo nome e raça, se houver só um.
	// Homônimos de verdade (os dois vivos) e mortos do Memorial ficam como estão.
	void reconcileCharacters()
	{
		DWORD now = GetTickCount();
		if (now - lastReconcile < 10000 || !ou || !ou->player)
			return;
		lastReconcile = now;

		std::map<std::string, std::vector<Character*> > byNameRace, byName;
		lektor<Character*>& squad = ou->player->playerCharacters;
		for (uint32_t i = 0; i < squad.size(); ++i)
		{
			Character* c = squad[i];
			if (!c)
				continue;
			std::string name = c->getName();
			byNameRace[name + "|" + raceName(c)].push_back(c);
			byName[name].push_back(c);
		}
		// Ainda carregando (o grupo aparece depois da tela de carregamento): sem ninguém, tudo viraria
		// "sem dono" e o resumo do log não serviria pra nada
		if (byName.empty())
			return;

		boost::lock_guard<boost::mutex> g(lock);
		bool firstRun = !reconciledOnce;
		reconciledOnce = true;
		std::vector<Stats::CharKey> keys = Stats::characterKeys();
		std::map<std::string, int> nameCount;
		for (size_t i = 0; i < keys.size(); ++i)
			++nameCount[keys[i].name];

		int live = 0, stale = 0, merged = 0, orphan = 0;
		for (size_t i = 0; i < keys.size(); ++i)
		{
			const Stats::CharKey& k = keys[i];
			if (k.fallen)
				continue;
			hand h;
			h.fromString(k.key);
			Character* current = h.getCharacter();
			std::string to;
			if (current && current->isPlayerCharacter() && current->getName() == k.name)
			{
				// O handle antigo ainda acha o personagem, mas o texto do handle atual é outro (trocou de
				// esquadrão): as duas entradas são da mesma pessoa e vão pro handle atual.
				++live;
				to = charKey(current);
			}
			else
			{
				++stale;
				// Entradas antigas sem raça casam só pelo nome
				std::map<std::string, std::vector<Character*> >& source = k.race.empty() ? byName : byNameRace;
				std::map<std::string, std::vector<Character*> >::iterator m =
					source.find(k.race.empty() ? k.name : k.name + "|" + k.race);
				if (m == source.end() || m->second.size() != 1)
				{
					++orphan; // ninguém (dispensado?) ou mais de um candidato: não dá pra saber
					continue;
				}
				to = charKey(m->second[0]);
			}
			if (firstRun && nameCount[k.name] > 1)
				DebugLog("KenshiAchievements: conferindo '" + k.name + "' " + k.key + " -> " + to
					+ (current ? " (handle ainda acha o personagem)" : " (handle não acha mais ninguém)"));
			if (to == k.key)
				continue;
			DebugLog("KenshiAchievements: '" + k.name + "' mudou de handle (" + k.key + " -> " + to + "), contagens juntadas");
			Stats::mergeCharacter(k.key, to);
			++merged;
		}
		if (firstRun || merged)
		{
			std::ostringstream o;
			o << "KenshiAchievements: conferência de personagens: " << keys.size() << " entradas, " << live
				<< " com handle válido, " << stale << " sem, " << merged << " juntadas, " << orphan << " sem dono no grupo";
			DebugLog(o.str());
		}
	}

	void (*mainLoop_orig)(GameWorld*, float) = NULL;
	void mainLoop_hook(GameWorld* self, float time)
	{
		mainLoop_orig(self, time);

		static bool loggedLoop = false;
		if (!loggedLoop)
		{
			loggedLoop = true;
			dbg("mainLoop ativo (UI ok)");
		}

		// Import (#37): o Kenshi não passa por loadAllPlatoons nem newGame ao importar (visto no log), então
		// os contadores do save importado entram no primeiro frame do mundo novo
		if (!pendingImport.empty() && ou && ou->player)
		{
			resetAll();
			loadFromImportedSave();
			pendingImport.clear();
		}

		tickGameClock();
		reconcileCharacters();
		resolvePendingKOs();
		pollDowned();
		checkKnownNames();

		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (!gui)
			return;

		std::vector<Stats::Unlock> unlocks;
		{
			boost::lock_guard<boost::mutex> g(lock);
			unlocks = Stats::popUnlocks();

			// limpa golpes velhos
			DWORD now = GetTickCount();
			for (std::map<std::string, LastHit>::iterator it = lastPlayerHit.begin(); it != lastPlayerHit.end();)
			{
				if (now - it->second.tick > HIT_MEMORY_MS)
					lastPlayerHit.erase(it++);
				else
					++it;
			}
		}

		for (size_t i = 0; i < unlocks.size(); ++i)
		{
			Toast t;
			t.title = unlocks[i].title;
			t.body = unlocks[i].description;
			if (!unlocks[i].who.empty())
				t.body += "\n(" + unlocks[i].who + ")";
			toastQueue.push_back(t);
			dbg("conquista liberada: " + unlocks[i].title);
			log(Lang::tr("log.owner", "Achievement"), unlocks[i].title + " - " + unlocks[i].description);
		}
		if (!unlocks.empty())
			playUnlockSound();
		showNextToast(gui);

		if (exportRequested)
		{
			exportRequested = false;
			exportStats();
		}

		if (panelCloseRequested)
		{
			panelCloseRequested = false;
			if (panel)
				togglePanel(gui);
		}

		bool keyDown = (GetAsyncKeyState(panelKey) & 0x8000) != 0;
		if (keyDown && !keyWasDown && gameHasFocus() && !modifierHeld() && !typingInTextBox())
			togglePanel(gui);
		keyWasDown = keyDown;

		// Atualiza 1x por segundo (ou já, se trocou de aba / acabou de abrir)
		if (panelText && GetTickCount() - panelRefreshedAt >= 1000)
		{
			std::string text = buildReport();
			if (text != panelLastText)
			{
				// setCaption põe o cursor no fim e a caixa rola junto (#23). Ao abrir/trocar de aba
				// (panelLastText vazio) começa no topo; nas atualizações mantém onde o jogador deixou.
				size_t scroll = panelLastText.empty() ? 0 : panelText->getVScrollPosition();
				panelText->setCaption(text);
				panelText->setTextCursor(0);
				panelText->setVScrollPosition((std::min)(scroll, panelText->getVScrollRange()));
				panelLastText = text;
			}
			if (charTable && currentTab == TAB_STATS)
			{
				std::vector<Stats::CharRow> rows;
				{
					boost::lock_guard<boost::mutex> g(lock);
					rows = Stats::characterRows();
				}
				fillTable(rows);
			}
			if (currentTab == TAB_VICTIMS)
			{
				std::vector<Stats::VictimRow> races, factions;
				{
					boost::lock_guard<boost::mutex> g(lock);
					races = Stats::victimRows(false, victimsKey);
					factions = Stats::victimRows(true, victimsKey);
				}
				fillVictimTable(raceTable, raceSignature, races);
				fillVictimTable(factionTable, factionSignature, factions);
			}
			panelRefreshedAt = GetTickCount();
		}
	}

}

// Tem que ser macro: GetRealAddress precisa receber &Classe::funcao direto no ponto de uso.
// Passando por template/função, o ponteiro para membro vira cópia e o endereço cai dentro
// desta DLL em vez do stub exportado pelo KenshiLib (assert "Incorrect address").
#define HOOK(target, detour, original) \
	if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(target), (void*)(detour), (void**)(original))) \
		ErrorLog("KenshiAchievements: falhou hook de " #target)

__declspec(dllexport) void startPlugin()
{
	std::vector<std::string> errors;
	std::string dir = modDir();
	int n = Stats::loadAchievements(dir + "achievements.txt", errors);
	for (size_t i = 0; i < errors.size(); ++i)
		ErrorLog("KenshiAchievements: achievements.txt " + errors[i]);
	loadSound(dir);

	debug = Stats::setting("debug", "0") == "1";
	if (debug)
		DebugLog("KenshiAchievements: debug mode on");

	// Idioma do jogo -> textos do mod + nomes de raça/facção de volta pro inglês (#3)
	char cwd[MAX_PATH] = { 0 };
	GetCurrentDirectoryA(MAX_PATH, cwd);
	std::string gameDir = std::string(cwd) + "\\";
	std::string lang = Lang::gameLanguage(gameDir);
	std::string used = Lang::loadTexts(dir, lang);
	int names = (lang.empty() || lang.compare(0, 2, "en") == 0) ? 0
		: Lang::loadGameNames(gameDir + "locale\\" + lang + "\\gamedata.po");
	std::ostringstream lo;
	lo << "KenshiAchievements: game language '" << (lang.empty() ? "?" : lang) << "', texts '" << used
		<< "', " << names << " translated names mapped to English";
	DebugLog(lo.str());

	// @key (ou @tecla, nome antigo)
	std::string keyName = Stats::setting("key", Stats::setting("tecla", "F6"));
	int key = parseKey(keyName);
	if (key)
		panelKey = key;
	else
		ErrorLog("KenshiAchievements: unknown @key '" + keyName + "', using F6");

	std::ostringstream o;
	o << "KenshiAchievements v" << VERSION << ": " << n << " achievements loaded";
	DebugLog(o.str());

	HOOK(&Character::declareDead, &declareDead_hook, &declareDead_orig);
	HOOK(&MedicalSystem::knockout, &knockout_hook, &knockout_orig);
	HOOK(&Character::_NV_hitByMeleeAttack, &hitByMelee_hook, &hitByMelee_orig);
	HOOK(&Character::iShotYou, &iShotYou_hook, &iShotYou_orig);
	HOOK(&MedicalSystem::addWound, &addWound_hook, &addWound_orig);
	HOOK(&MedicalSystem::amputate, &amputate_hook, &amputate_orig);
	HOOK(&FactionManager::saveGameState, &saveGameState_hook, &saveGameState_orig);
	HOOK(&GameWorld::loadAllPlatoons, &loadAllPlatoons_hook, &loadAllPlatoons_orig);
	HOOK(&SaveManager::newGame, &newGame_hook, &newGame_orig);
	HOOK(&SaveManager::import, &import_hook, &import_orig);
	HOOK(&GameWorld::_NV_mainLoop_GPUSensitiveStuff, &mainLoop_hook, &mainLoop_orig);
}
