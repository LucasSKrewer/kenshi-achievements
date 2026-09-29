// Teste da lógica sem o jogo: Lang (dicionário .po + textos) e Stats (contagem, conquistas, abas).
// Compilar/rodar: powershell -File tests\rodar-testes.ps1
#include "../src/Lang.h"
#include "../src/Stats.h"

#include <cstdio>
#include <string>
#include <vector>

static int falhas = 0;

static void check(bool ok, const std::string& what)
{
	printf("%s  %s\n", ok ? "ok  " : "FALHOU", what.c_str());
	if (!ok)
		++falhas;
}

int main(int argc, char** argv)
{
	std::string game = argc > 1 ? argv[1] : "E:\\steam\\steamapps\\common\\Kenshi\\";
	std::string mod = argc > 2 ? argv[2] : "mod\\KenshiAchievements\\";

	// Idioma e dicionário do jogo
	std::string lang = Lang::gameLanguage(game);
	printf("idioma do jogo: '%s'\n", lang.c_str());
	int n = Lang::loadGameNames(game + "locale\\pt_BR\\gamedata.po");
	printf("nomes mapeados: %d\n", n);
	check(n > 100, "carregou nomes de RACE/FACTION do gamedata.po");
	check(Lang::toEnglish("Bandidos da Poeira") == "Dust Bandits", "Bandidos da Poeira -> Dust Bandits");
	check(Lang::toEnglish("Camponês") == "Greenlander", "Camponês -> Greenlander");
	check(Lang::toEnglish("Beduíno") == "Scorchlander", "Beduíno -> Scorchlander");
	check(Lang::toEnglish("Shek") == "Shek", "Shek continua Shek");
	check(Lang::toEnglish("Leviatã") == "Leviathan", "Leviatã -> Leviathan (só tem entrada ANIMAL_CHARACTER no .po)");
	check(Lang::toEnglish("Raptor do Pântano") == "Swamp Raptor", "Raptor do Pântano -> Swamp Raptor (visto no jogo como raça)");
	check(Lang::toEnglish("Tartaruga do Pântano") == "Swamp Turtle", "Tartaruga do Pântano -> Swamp Turtle (RACE)");
	check(Lang::toEnglish("Bicudo") == "Beak Thing", "Bicudo -> Beak Thing (visto no save do jogo)");
	check(Lang::toEnglish("Skeleton Screamer MKII") == "Skeleton Screamer MKII", "nome do Genesis sem tradução passa direto");
	printf("  Nação Sagrada? -> '%s'\n", Lang::toEnglish("Nação Sagrada").c_str());

	// Textos do mod
	std::string used = Lang::loadTexts(mod, "pt_BR");
	check(used == "pt_BR", "carregou lang/pt_BR.txt");
	check(Lang::tr("tab.achievements", "?") == "Conquistas", "texto traduzido da aba");
	check(Lang::tr("first_blood.title", "?") == "Primeiro Sangue", "título de conquista traduzido");
	Lang::loadTexts(mod, "de_DE");
	check(Lang::tr("tab.achievements", "?") == "Achievements", "idioma sem arquivo cai no inglês");
	Lang::loadTexts(mod, "pt_BR");

	// Conquistas + contagem
	std::vector<std::string> errors;
	int ach = Stats::loadAchievements(mod + "achievements.txt", errors);
	for (size_t i = 0; i < errors.size(); ++i)
		printf("  erro: %s\n", errors[i].c_str());
	check(ach == 45 && errors.empty(), "45 conquistas sem erro");
	check(Stats::setting("key", "?") == "F6", "@key = F6");

	Stats::reset();
	Stats::recordKill("h1", "The Arbiter", "Skeleton Screamer MKII", Lang::toEnglish("Camponês"), Lang::toEnglish("Bandidos da Poeira"));
	Stats::recordKill("h1", "The Arbiter", "Skeleton Screamer MKII", Lang::toEnglish("Beduíno"), Lang::toEnglish("Bandidos da Poeira"));
	Stats::recordKill("h2", "Brooke", "White Direwolf", "Skeleton Screamer MKII", "Renegados");
	Stats::recordKO("h2", "Brooke", "White Direwolf", "Shek", Lang::toEnglish("Bandidos da Poeira"));
	std::vector<Stats::Unlock> u = Stats::popUnlocks();
	check(u.size() == 2, "liberou First Blood + Good Night");
	for (size_t i = 0; i < u.size(); ++i)
		printf("  liberada: %s - %s (%s)\n", u[i].title.c_str(), u[i].description.c_str(), u[i].who.c_str());

	std::string stats = Stats::statsReport("h1", "The Arbiter");
	std::string achs = Stats::achievementsReport();
	check(achs.find("Poeira Assentada  (2/25)") != std::string::npos, "Dust Bandits contou kills traduzidas (2/25)");
	check(achs.find("Sucateiro  (1/10)") != std::string::npos, "*Skeleton* pegou o Screamer MKII (1/10)");

	// Homônimos (visto no jogo: dois "The Arbiter" de raças diferentes): contagens separadas,
	// raça no nome pra diferenciar, e a raça sobrevive ao save/load.
	{
		Stats::reset();
		Stats::recordKill("hA", "The Arbiter", "Skeleton Screamer MKII", "Shek", "Dust Bandits");
		Stats::recordKill("hA", "The Arbiter", "Skeleton Screamer MKII", "Shek", "Dust Bandits");
		Stats::recordKill("hB", "The Arbiter", "Skeleton MKI", "Shek", "Dust Bandits");
		Stats::recordKill("hC", "Kang", "Shek", "Shek", "Dust Bandits");
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		Stats::exportTo(ints, strs);
		Stats::importFrom(ints, strs);
		std::string r = Stats::statsReport("hB", "The Arbiter");
		check(r.find("The Arbiter (Skeleton Screamer MKII):  Kills: 2") != std::string::npos, "homônimo A separado (2)");
		check(r.find("The Arbiter (Skeleton MKI):  Kills: 1") != std::string::npos, "homônimo B separado (1)");
		check(r.find("The Arbiter (Skeleton MKI)\n   Kills: 1") != std::string::npos, "selecionado B mostra só as dele");
		check(r.find("Kang:  Kills: 1") != std::string::npos, "nome único sem raça");
		printf("\n===== homônimos =====\n%s", r.c_str());
		Stats::popUnlocks();
	}

	// #7: conquistas de conteúdo ausente ficam ocultas
	{
		Stats::reset();
		std::set<std::string> races, factions;
		races.insert("Shek");
		races.insert("Skeleton MKI");     // *Skeleton* existe
		factions.insert("Dust Bandits");
		factions.insert("The Holy Nation");
		std::vector<std::string> hidden = Stats::setKnownNames(races, factions);  // jogo "pelado": sem Genesis, sem Beak Thing
		std::set<std::string> h(hidden.begin(), hidden.end());
		check(h.count("beak_hunter") && h.count("primordial") && h.count("wolven_order") && h.count("giant_slayer"),
			"sem o conteúdo: beak_hunter e as do Genesis ocultas");
		check(!h.count("first_blood") && !h.count("shek_slayer") && !h.count("skeleton_bane") && !h.count("bandit_bane"),
			"gerais e as de conteúdo presente continuam");
		std::string r = Stats::achievementsReport();
		check(r.find("Caçador de Bicudos") == std::string::npos, "oculta não aparece no painel");
		char esperado[64];
		sprintf(esperado, "Concluídas: 0/%d", 45 - (int)hidden.size());
		check(r.find(esperado) != std::string::npos, std::string("total ignora as ocultas (") + esperado + ")");

		// Já liberada num save continua visível, mesmo com o conteúdo ausente
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		ints["a:beak_hunter"] = 1;
		ints["a:id_que_nao_existe_mais"] = 1;
		Stats::importFrom(ints, strs);
		r = Stats::achievementsReport();
		check(r.find("[X] Caçador de Bicudos") != std::string::npos, "liberada continua visível");
		sprintf(esperado, "Concluídas: 1/%d", 45 - (int)hidden.size() + 1);
		check(r.find(esperado) != std::string::npos, std::string("resumo ignora id que não existe mais (") + esperado + ")");

		// Nomes vazios (dados não lidos) = nada oculto
		hidden = Stats::setKnownNames(std::set<std::string>(), std::set<std::string>());
		check(hidden.empty(), "sem dados do jogo, nada fica oculto");
		Stats::reset();
	}

	// Save antigo com nome traduzido + nome em inglês: normaliza e soma
	{
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		ints["f.k:Bandidos da Poeira"] = 3;
		ints["f.k:Dust Bandits"] = 2;
		Stats::importFrom(ints, strs);
		std::string r = Stats::achievementsReport();
		check(r.find("Poeira Assentada  (5/25)") != std::string::npos, "save antigo: Bandidos da Poeira + Dust Bandits = 5/25");
		Stats::reset();
	}

	// #8: conquistas secretas
	{
		Stats::reset();
		std::string r = Stats::achievementsReport();
		check(r.find("Regicídio") == std::string::npos && r.find("Mate uma Rainha da Colônia.") == std::string::npos,
			"secreta pendente não revela título nem descrição");
		check(r.find("[?] ???  (conquista secreta)") != std::string::npos, "secreta aparece como ???");
		Stats::recordKill("hX", "Fuu", "Greenlander", "Hive Queen", "Western Hive");
		std::vector<Stats::Unlock> un = Stats::popUnlocks();
		bool gotRegicide = false;
		for (size_t i = 0; i < un.size(); ++i)
			if (un[i].title == "Regicídio")
				gotRegicide = true;
		check(gotRegicide, "secreta libera com título normal (Regicídio)");
		r = Stats::achievementsReport();
		check(r.find("[X] Regicídio") != std::string::npos, "liberada aparece com o nome");
		std::map<std::string, int> ints;
		std::map<std::string, std::string> strs;
		Stats::exportTo(ints, strs);
		check(ints.count("a:regicide") == 1 && ints.count("a:?regicide") == 0, "id salvo sem o '?'");
		Stats::reset();
	}

	printf("\n===== aba Estatísticas (The Arbiter selecionado) =====\n%s", stats.c_str());
	printf("===== aba Conquistas =====\n%s", achs.c_str());
	printf("===== nenhum selecionado =====\n%s", Stats::statsReport("", "").c_str());

	printf("\n%s (%d falha(s))\n", falhas ? "FALHOU" : "TUDO OK", falhas);
	return falhas ? 1 : 0;
}
