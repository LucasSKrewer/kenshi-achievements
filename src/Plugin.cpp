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

#include "Stats.h"

#include <Debug.h>
#include <core/Functions.h>

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
#include <kenshi/gui/ManagementScreen.h>
#include <kenshi/util/hand.h>

#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_Window.h>

#include <boost/thread/lock_guard.hpp>
#include <boost/thread/mutex.hpp>

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
	const int PANEL_KEY = VK_F9;

	boost::mutex lock; // hooks de combate podem vir de outras threads

	struct LastHit
	{
		hand attacker;
		DWORD tick;
	};
	std::map<std::string, LastHit> lastPlayerHit; // chave = handle da vítima
	std::set<std::string> countedDeaths;

	// ---------- utilitários ----------

	std::string handleKey(RootObjectBase* o)
	{
		return o->getHandle().toString();
	}

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

	// Quem do jogador derrubou a vítima? NULL se não foi o jogador.
	Character* findPlayerAttacker(Character* victim)
	{
		Character* c = victim->lastGuyWhoDefeatedMe.getCharacter();
		if (c && c->isPlayerCharacter())
			return c;

		std::map<std::string, LastHit>::iterator it = lastPlayerHit.find(handleKey(victim));
		if (it != lastPlayerHit.end() && GetTickCount() - it->second.tick <= HIT_MEMORY_MS)
		{
			c = it->second.attacker.getCharacter();
			if (c && c->isPlayerCharacter())
				return c;
		}
		return NULL;
	}

	void onTakedown(Character* victim, bool kill)
	{
		if (!victim || victim->isPlayerCharacter())
			return;

		boost::lock_guard<boost::mutex> g(lock);

		std::string victimKey = handleKey(victim);
		if (kill && !countedDeaths.insert(victimKey).second)
			return;

		Character* attacker = findPlayerAttacker(victim);
		if (!attacker)
			return;

		std::string key = handleKey(attacker);
		std::string name = attacker->getName();
		if (kill)
			Stats::recordKill(key, name, raceName(victim), factionName(victim));
		else
			Stats::recordKO(key, name, raceName(victim), factionName(victim));

		if (kill)
			lastPlayerHit.erase(victimKey);
	}

	void rememberHit(Character* victim, Character* attacker)
	{
		if (!victim || !attacker || !attacker->isPlayerCharacter() || victim->isPlayerCharacter())
			return;
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
	}

	// ---------- hooks de combate ----------

	void (*declareDead_orig)(Character*) = NULL;
	void declareDead_hook(Character* self)
	{
		declareDead_orig(self);
		onTakedown(self, true);
	}

	void (*knockout_orig)(MedicalSystem*, float) = NULL;
	void knockout_hook(MedicalSystem* self, float skill01)
	{
		bool wasDown = self->unconcious || self->dead;
		knockout_orig(self, skill01);
		if (!wasDown && self->unconcious && !self->dead)
			onTakedown(self->me, false);
	}

	HitMaterialType (*hitByMelee_orig)(Character*, CutDirection, Damages&, Character*, CombatTechniqueData*, int) = NULL;
	HitMaterialType hitByMelee_hook(Character* self, CutDirection dir, Damages& damage, Character* who, CombatTechniqueData* attack, int comboID)
	{
		rememberHit(self, who);
		return hitByMelee_orig(self, dir, damage, who, attack, comboID);
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
	DWORD panelRefreshedAt = 0;
	bool keyWasDown = false;

	// Som da conquista (@som no achievements.txt):
	//   arquivo .wav na pasta do mod -> tocado pelo Windows (padrão: achievement.wav)
	//   nome de evento Wwise do Kenshi (ex.: Change_Level) -> tocado pelo motor de áudio do jogo
	//   "nenhum" -> mudo
	std::vector<char> soundWav;
	std::string soundEvent;

	void loadSound(const std::string& dir)
	{
		std::string s = Stats::setting("som", "achievement.wav");
		if (s == "nenhum")
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
		soundEvent = s;
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
		if (c)
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
		toastWindow->setCaption("Conquista desbloqueada!");
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
			return;
		}
		panel = gui->createWidgetReal<MyGUI::Window>("Kenshi_WindowCX", 0.30f, 0.15f, 0.40f, 0.65f,
			MyGUI::Align::Default, "Overlapped", "KenshiAchievementsPanel");
		panel->setCaption("Kills & Conquistas  (F9 fecha)");
		panelText = panel->getClientWidget()->createWidgetReal<MyGUI::EditBox>(
			"Kenshi_EditBox", 0.02f, 0.02f, 0.96f, 0.96f, MyGUI::Align::Stretch);
		panelText->setEditReadOnly(true);
		panelText->setEditMultiLine(true);
		panelText->setEditWordWrap(true);
		panelRefreshedAt = 0; // força refresh
	}

	bool gameHasFocus()
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		return pid == GetCurrentProcessId();
	}

	void (*mainLoop_orig)(GameWorld*, float) = NULL;
	void mainLoop_hook(GameWorld* self, float time)
	{
		mainLoop_orig(self, time);

		MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
		if (!gui)
			return;

		std::vector<Stats::Unlock> unlocks;
		std::string report;
		bool refresh = panelText && GetTickCount() - panelRefreshedAt >= 1000;
		{
			boost::lock_guard<boost::mutex> g(lock);
			unlocks = Stats::popUnlocks();
			if (refresh)
				report = Stats::report();

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
			log("Conquista", unlocks[i].title + " - " + unlocks[i].description);
		}
		if (!unlocks.empty())
			playUnlockSound();
		showNextToast(gui);

		bool keyDown = (GetAsyncKeyState(PANEL_KEY) & 0x8000) != 0;
		if (keyDown && !keyWasDown && gameHasFocus())
		{
			togglePanel(gui);
			refresh = panelText != NULL;
			if (refresh)
			{
				boost::lock_guard<boost::mutex> g(lock);
				report = Stats::report();
			}
		}
		keyWasDown = keyDown;

		if (refresh && panelText)
		{
			panelText->setCaption(report);
			panelRefreshedAt = GetTickCount();
		}
	}

	template <typename T>
	void hook(T target, void* detour, void* original, const char* name)
	{
		if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(target), detour, (void**)original))
			ErrorLog(std::string("KenshiAchievements: falhou hook de ") + name);
	}
}

__declspec(dllexport) void startPlugin()
{
	std::vector<std::string> errors;
	std::string dir = modDir();
	int n = Stats::loadAchievements(dir + "achievements.txt", errors);
	for (size_t i = 0; i < errors.size(); ++i)
		ErrorLog("KenshiAchievements: achievements.txt " + errors[i]);
	loadSound(dir);

	std::ostringstream o;
	o << "KenshiAchievements: " << n << " conquistas carregadas";
	DebugLog(o.str());

	hook(&Character::declareDead, (void*)&declareDead_hook, &declareDead_orig, "Character::declareDead");
	hook(&MedicalSystem::knockout, (void*)&knockout_hook, &knockout_orig, "MedicalSystem::knockout");
	hook(&Character::_NV_hitByMeleeAttack, (void*)&hitByMelee_hook, &hitByMelee_orig, "Character::hitByMeleeAttack");
	hook(&Character::iShotYou, (void*)&iShotYou_hook, &iShotYou_orig, "Character::iShotYou");
	hook(&FactionManager::saveGameState, (void*)&saveGameState_hook, &saveGameState_orig, "FactionManager::saveGameState");
	hook(&GameWorld::loadAllPlatoons, (void*)&loadAllPlatoons_hook, &loadAllPlatoons_orig, "GameWorld::loadAllPlatoons");
	hook(&SaveManager::newGame, (void*)&newGame_hook, &newGame_orig, "SaveManager::newGame");
	hook(&GameWorld::_NV_mainLoop_GPUSensitiveStuff, (void*)&mainLoop_hook, &mainLoop_orig, "GameWorld::mainLoop_GPUSensitiveStuff");
}
