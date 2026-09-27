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
	check(ach == 16 && errors.empty(), "16 conquistas sem erro");
	check(Stats::setting("key", "?") == "F6", "@key = F6");

	Stats::reset();
	Stats::recordKill("h1", "The Arbiter", Lang::toEnglish("Camponês"), Lang::toEnglish("Bandidos da Poeira"));
	Stats::recordKill("h1", "The Arbiter", Lang::toEnglish("Beduíno"), Lang::toEnglish("Bandidos da Poeira"));
	Stats::recordKill("h2", "Brooke", "Skeleton Screamer MKII", "Renegados");
	Stats::recordKO("h2", "Brooke", "Shek", Lang::toEnglish("Bandidos da Poeira"));
	std::vector<Stats::Unlock> u = Stats::popUnlocks();
	check(u.size() == 2, "liberou First Blood + Good Night");
	for (size_t i = 0; i < u.size(); ++i)
		printf("  liberada: %s - %s (%s)\n", u[i].title.c_str(), u[i].description.c_str(), u[i].who.c_str());

	std::string stats = Stats::statsReport("h1", "The Arbiter");
	std::string achs = Stats::achievementsReport();
	check(achs.find("Poeira Assentada  (2/25)") != std::string::npos, "Dust Bandits contou kills traduzidas (2/25)");
	check(achs.find("Sucateiro  (1/10)") != std::string::npos, "*Skeleton* pegou o Screamer MKII (1/10)");

	printf("\n===== aba Estatísticas (The Arbiter selecionado) =====\n%s", stats.c_str());
	printf("===== aba Conquistas =====\n%s", achs.c_str());
	printf("===== nenhum selecionado =====\n%s", Stats::statsReport("", "").c_str());

	printf("\n%s (%d falha(s))\n", falhas ? "FALHOU" : "TUDO OK", falhas);
	return falhas ? 1 : 0;
}
