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
#include <kenshi/SaveManager.h>
#include <kenshi/Tasker.h>
#include <kenshi/gui/ManagementScreen.h>
#include <kenshi/util/hand.h>

#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>

#include <cctype>
#include <cstdlib>
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
	int panelKey = VK_F6; // @tecla no achievements.txt. F9 é o quickload do Kenshi, F5 o quicksave.

	boost::mutex lock; // hooks de combate podem vir de outras threads

	struct LastHit
	{
		hand attacker;
		DWORD tick;
	};
	std::map<std::string, LastHit> lastPlayerHit; // chave = handle da vítima
	std::set<std::string> countedDeaths;
	const DWORD KO_DEDUPE_MS = 10000; // knockout() repetido na mesma vítima em menos que isso não conta de novo
	std::map<std::string, DWORD> lastKO; // chave = handle da vítima

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

	void countTakedown(Character* victim, Character* attacker, bool kill);

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
	Character* findPlayerAttacker(Character* victim, bool verbose = true)
	{
		Character* stealth = findStealthAttacker(victim, verbose);
		if (stealth)
			return stealth;

		Character* c = victim->lastGuyWhoDefeatedMe.getCharacter();
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

	void onTakedown(Character* victim, bool kill)
	{
		if (!victim)
			return;
		dbg(std::string(kill ? "MORTE" : "KO") + " de " + describe(victim));
		if (victim->isPlayerCharacter())
		{
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
		}

		Character* attacker = findPlayerAttacker(victim);
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
		countTakedown(victim, attacker, kill);
		if (kill)
			lastPlayerHit.erase(victimKey);
	}

	// Chamar com o lock seguro.
	void countTakedown(Character* victim, Character* attacker, bool kill)
	{
		std::string key = charKey(attacker);
		dbg("  CONTADO para " + describe(attacker) + " id=" + key);
		std::string name = attacker->getName();
		if (kill)
			Stats::recordKill(key, name, raceName(attacker), Lang::toEnglish(raceName(victim)), Lang::toEnglish(factionName(victim)));
		else
			Stats::recordKO(key, name, raceName(attacker), Lang::toEnglish(raceName(victim)), Lang::toEnglish(factionName(victim)));
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
					countTakedown(victim, attacker, false);
				}
				else if (victim)
				{
					dbg("KO de " + describe(victim) + " NÃO contado: ninguém do jogador em 3 s (lastGuyWhoDefeatedMe = "
						+ describe(victim->lastGuyWhoDefeatedMe.getCharacter()) + ")");
				}
				pendingKOs.erase(pendingKOs.begin() + i);
			}
			else
			{
				++i;
			}
		}
	}

	void rememberHit(Character* victim, Character* attacker)
	{
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
		h.tick = GetTickCount();
		lastPlayerHit[handleKey(victim)] = h;
	}

	void resetAll()
	{
		boost::lock_guard<boost::mutex> g(lock);
		Stats::reset();
		lastPlayerHit.clear();
		countedDeaths.clear();
		lastKO.clear();
		pendingKOs.clear();
	}

	// ---------- hooks de combate ----------

	void (*declareDead_orig)(Character*) = NULL;
	void declareDead_hook(Character* self)
	{
		declareDead_orig(self);
		dbg("declareDead() chamado");
		onTakedown(self, true);
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

	void (*loadAllPlatoons_orig)(GameWorld*) = NULL;
	void loadAllPlatoons_hook(GameWorld* self)
	{
		resetAll();
		loadAllPlatoons_orig(self);

		auto it = ou->savedata.gamedataSID.find(SAVE_SID);
		if (it == ou->savedata.gamedataSID.end() || !it->second)
		{
			DebugLog("KenshiAchievements: save sem contadores (save novo ou anterior ao mod)");
			return;
		}

		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		for (auto i = it->second->idata.begin(); i != it->second->idata.end(); ++i)
			ints[i->first] = i->second;
		for (auto s = it->second->sdata.begin(); s != it->second->sdata.end(); ++s)
			strs[s->first] = s->second;

		boost::lock_guard<boost::mutex> g(lock);
		Stats::importFrom(ints, strs);
		DebugLog("KenshiAchievements: contadores carregados do save");
	}

	void (*newGame_orig)(SaveManager*, const std::string&) = NULL;
	void newGame_hook(SaveManager* self, const std::string& startId)
	{
		resetAll();
		newGame_orig(self, startId);
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
	int currentTab = 0; // 0 = estatísticas, 1 = conquistas (lembrado entre aberturas)
	std::string panelLastText; // só reescreve o texto quando muda (setCaption volta a rolagem pro topo)
	bool panelCloseRequested = false;
	DWORD panelRefreshedAt = 0;

	void onPanelButton(MyGUI::Window* sender, const std::string& name)
	{
		// Não destrói aqui: estamos dentro do evento do próprio widget. Fecha no próximo frame.
		if (name == "close")
			panelCloseRequested = true;
	}

	void selectTab(int tab)
	{
		currentTab = tab;
		if (tabStats)
			tabStats->setStateSelected(tab == 0);
		if (tabAchievements)
			tabAchievements->setStateSelected(tab == 1);
		panelRefreshedAt = 0; // força refresh no próximo frame
	}

	void onTabClick(MyGUI::Widget* sender)
	{
		selectTab(sender == tabAchievements ? 1 : 0);
	}

	// Texto da aba atual. Chamar na thread principal (lê o personagem selecionado).
	std::string buildReport()
	{
		if (currentTab == 1)
		{
			boost::lock_guard<boost::mutex> g(lock);
			return Stats::achievementsReport();
		}
		std::string selKey, selName;
		Character* sel = (ou && ou->player) ? ou->player->selectedCharacter.getCharacter() : NULL;
		if (sel)
		{
			selKey = charKey(sel);
			selName = sel->getName();
		}
		boost::lock_guard<boost::mutex> g(lock);
		return Stats::statsReport(selKey, selName);
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
			return;
		}
		panel = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.30f, 0.15f, 0.40f, 0.65f,
			MyGUI::Align::Default, "Overlapped", "KenshiAchievementsPanel");
		panel->setCaption(Lang::tr("panel.title", "Kills & Achievements"));
		MyGUI::Widget* client = panel->getClientWidget();

		// Abas: dois botões no topo, o da aba atual fica "selecionado"
		tabStats = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.02f, 0.01f, 0.47f, 0.07f,
			MyGUI::Align::Top | MyGUI::Align::HStretch);
		tabStats->setCaption(Lang::tr("tab.stats", "Statistics"));
		tabStats->eventMouseButtonClick += MyGUI::newDelegate(onTabClick);
		tabAchievements = client->createWidgetReal<MyGUI::Button>("Kenshi_Button1", 0.51f, 0.01f, 0.47f, 0.07f,
			MyGUI::Align::Top | MyGUI::Align::HStretch);
		tabAchievements->setCaption(Lang::tr("tab.achievements", "Achievements"));
		tabAchievements->eventMouseButtonClick += MyGUI::newDelegate(onTabClick);

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
		std::set<std::string> races, factions;
		for (auto it = ou->gamedata.gamedataSID.begin(); it != ou->gamedata.gamedataSID.end(); ++it)
		{
			GameData* d = it->second;
			if (!d)
				continue;
			if (d->type == RACE)
				races.insert(Lang::toEnglish(d->name));
			else if (d->type == FACTION)
				factions.insert(Lang::toEnglish(d->name));
		}
		std::vector<std::string> hidden;
		{
			boost::lock_guard<boost::mutex> g(lock);
			hidden = Stats::setKnownNames(races, factions);
		}
		std::ostringstream o;
		o << "KenshiAchievements: " << races.size() << " races, " << factions.size() << " factions in game data; "
			<< hidden.size() << " achievement(s) hidden (content not installed)";
		for (size_t i = 0; i < hidden.size(); ++i)
			o << (i ? ", " : ": ") << hidden[i];
		DebugLog(o.str());
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

		resolvePendingKOs();
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
				panelText->setCaption(text);
				panelLastText = text;
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
	o << "KenshiAchievements: " << n << " achievements loaded";
	DebugLog(o.str());

	HOOK(&Character::declareDead, &declareDead_hook, &declareDead_orig);
	HOOK(&MedicalSystem::knockout, &knockout_hook, &knockout_orig);
	HOOK(&Character::_NV_hitByMeleeAttack, &hitByMelee_hook, &hitByMelee_orig);
	HOOK(&Character::iShotYou, &iShotYou_hook, &iShotYou_orig);
	HOOK(&MedicalSystem::addWound, &addWound_hook, &addWound_orig);
	HOOK(&FactionManager::saveGameState, &saveGameState_hook, &saveGameState_orig);
	HOOK(&GameWorld::loadAllPlatoons, &loadAllPlatoons_hook, &loadAllPlatoons_orig);
	HOOK(&SaveManager::newGame, &newGame_hook, &newGame_orig);
	HOOK(&GameWorld::_NV_mainLoop_GPUSensitiveStuff, &mainLoop_hook, &mainLoop_orig);
}
