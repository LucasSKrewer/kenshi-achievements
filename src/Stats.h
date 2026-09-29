#pragma once

// Estado do mod: contadores de kills/KOs e conquistas.
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
		CharStats() : kills(0), kos(0) {}
	};

	struct Achievement
	{
		std::string id;
		std::string metric; // kills, kos, takedowns, char_kills, char_kos, race_kills, race_kos, faction_kills, faction_kos
		std::string arg;    // raça/facção para race_* e faction_*
		int target;
		std::string title;
		std::string description;
		bool available; // false = cita raça/facção que não existe no jogo carregado (mod ausente): fica oculta
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
	// Linhas "@chave = valor" viram configurações (ver setting()).
	int loadAchievements(const std::string& path, std::vector<std::string>& errors);

	// Nomes em inglês das raças/facções que existem nos dados carregados (jogo + mods). Conquistas
	// race_*/faction_* sem nenhum nome correspondente ficam ocultas (ex.: conquistas do Genesis sem
	// o Genesis instalado). Retorna os ids ocultados.
	std::vector<std::string> setKnownNames(const std::set<std::string>& races, const std::set<std::string>& factions);

	// Configuração lida do achievements.txt, ou `fallback` se ausente.
	std::string setting(const std::string& key, const std::string& fallback);

	void reset();

	// key = handle do personagem do jogador (estável entre saves — conferido no jogo); name/charRace só
	// para exibição (pode haver homônimos, então nunca identificar pelo nome); race/faction = da vítima.
	void recordKill(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction);
	void recordKO(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction);

	// Conquistas desbloqueadas desde a última chamada.
	std::vector<Unlock> popUnlocks();

	// Serialização chave/valor (vai para o GameData dentro do save).
	void exportTo(std::map<std::string, int>& ints, std::map<std::string, std::string>& strs);
	void importFrom(const std::map<std::string, int>& ints, const std::map<std::string, std::string>& strs);

	// Aba "Estatísticas": personagem selecionado (key/name vazios = nenhum), total do grupo e lista.
	std::string statsReport(const std::string& selectedKey, const std::string& selectedName);

	// Aba "Conquistas": concluídas e pendentes com progresso.
	std::string achievementsReport();
}
