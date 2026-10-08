#pragma once

// Estado do mod: contadores (kills, KOs, membros decepados...) e conquistas.
// Não depende de nada do Kenshi, então dá pra testar fora do jogo.
// Não é thread-safe: quem chama (Plugin.cpp) segura o lock.
// Compila no VS2010 (v100): nada de range-for, initializer list ou enum class.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Stats
{
	struct CharStats
	{
		std::string name;
		std::string race; // do personagem, pra diferenciar homônimos no painel ("The Arbiter (Skeleton MKI)")
		int kills;
		int kos;
		int stealthKos; // parte dos kos feita por nocaute furtivo
		int limbs;      // membros decepados
		CharStats() : kills(0), kos(0), stealthKos(0), limbs(0) {}
	};

	// Uma kill ou um KO. Nomes de raça/facção/NPC já em inglês (Lang::toEnglish).
	struct Takedown
	{
		std::string key, name, charRace; // quem fez (key = handle; name/charRace só pra exibir)
		std::string race, faction;       // da vítima
		std::string npc;                 // nome da vítima se for NPC único; vazio caso contrário
		bool stealth;                    // nocaute/assassinato furtivo
		double time;                     // relógio do jogo em segundos (pra sequências); < 0 = desconhecido
		Takedown() : stealth(false), time(-1.0) {}
	};

	struct Achievement
	{
		std::string id;
		// kills, kos, takedowns, limbs, stealth_kos                     -> grupo
		// char_kills, char_kos, char_limbs, char_stealth_kos            -> melhor personagem
		// race_kills, race_kos, faction_kills, faction_kos, npc_kills, npc_kos, npc_takedowns : <nome> (aceita * e ,)
		// burst_kills, burst_takedowns : <segundos>                     -> recorde de N em X segundos de jogo
		std::string metric;
		std::string arg;
		int target;
		std::string title;
		std::string description;
		std::string category; // linha "[Categoria]" no achievements.txt; agrupa no painel
		bool available; // false = cita raça/facção/NPC que não existe no jogo carregado (mod ausente): fica oculta
		bool secret;    // "?id" no achievements.txt: aparece como ??? até ser liberada
		Achievement() : target(0), available(true), secret(false) {}
	};

	struct Unlock
	{
		std::string title;
		std::string description;
		std::string who; // personagem que desbloqueou (pode ser vazio)
	};

	// Carrega achievements.txt. Retorna quantas foram lidas; erros vão pra `errors`.
	// Linhas "@chave = valor" viram configurações (ver setting()); "[Nome]" abre uma categoria.
	int loadAchievements(const std::string& path, std::vector<std::string>& errors);

	// Nomes em inglês das raças/facções/personagens que existem nos dados carregados (jogo + mods).
	// Conquistas race_*/faction_*/npc_* sem nenhum nome correspondente ficam ocultas (ex.: conquistas do
	// Genesis sem o Genesis instalado). Conjunto vazio = não sabe, não oculta. Retorna os ids ocultados.
	std::vector<std::string> setKnownNames(const std::set<std::string>& races, const std::set<std::string>& factions,
		const std::set<std::string>& npcs = std::set<std::string>());

	// Configuração lida do achievements.txt, ou `fallback` se ausente.
	std::string setting(const std::string& key, const std::string& fallback);

	void reset();

	void recordKill(const Takedown& t);
	void recordKO(const Takedown& t);
	// Membro decepado por um personagem do jogador.
	void recordLimb(const std::string& key, const std::string& name, const std::string& charRace);

	// Atalhos antigos (sem NPC, furtivo ou relógio).
	void recordKill(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction);
	void recordKO(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction);

	// Conquistas desbloqueadas desde a última chamada.
	std::vector<Unlock> popUnlocks();

	// Serialização chave/valor (vai para o GameData dentro do save).
	void exportTo(std::map<std::string, int>& ints, std::map<std::string, std::string>& strs);
	void importFrom(const std::map<std::string, int>& ints, const std::map<std::string, std::string>& strs);

	// Aba "Estatísticas": personagem selecionado (key/name vazios = nenhum), total do grupo, lista por
	// personagem e vítimas mais frequentes.
	std::string statsReport(const std::string& selectedKey, const std::string& selectedName);

	// Aba "Conquistas": por categoria, concluídas e pendentes com progresso.
	std::string achievementsReport();
}
