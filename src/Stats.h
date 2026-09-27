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
		Achievement() : target(0) {}
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

	// Configuração lida do achievements.txt, ou `fallback` se ausente.
	std::string setting(const std::string& key, const std::string& fallback);

	void reset();

	// key = handle do personagem do jogador (estável entre saves); name só para exibição.
	void recordKill(const std::string& key, const std::string& name, const std::string& race, const std::string& faction);
	void recordKO(const std::string& key, const std::string& name, const std::string& race, const std::string& faction);

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
