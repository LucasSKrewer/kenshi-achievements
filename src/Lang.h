#pragma once

// Idioma: textos do mod (lang/<idioma>.txt) e nomes do jogo traduzidos de volta pro inglês.
// Não depende de nada do Kenshi. Compila no VS2010.

#include <string>

namespace Lang
{
	// Idioma escolhido no jogo (settings.cfg -> language=pt_BR). "" se não achar.
	std::string gameLanguage(const std::string& gameDir);

	// Carrega os textos do mod: tenta lang/<lang>.txt, depois lang/<2 letras>.txt, sempre por cima do lang/en.txt.
	// Retorna o arquivo de idioma usado (ou "en").
	std::string loadTexts(const std::string& modDir, const std::string& lang);

	// Texto traduzido de `key`, ou `fallback` se não existir.
	std::string tr(const std::string& key, const std::string& fallback);

	// Troca {nome} por `value` em `text`.
	std::string fill(const std::string& text, const char* name, const std::string& value);
	std::string fill(const std::string& text, const char* name, int value);

	// Lê locale/<lang>/gamedata.po do jogo e monta tradução -> inglês para RACE e FACTION.
	// Retorna quantos nomes foram carregados.
	int loadGameNames(const std::string& poPath);

	// Nome de raça/facção em inglês (se veio traduzido), senão o próprio nome.
	std::string toEnglish(const std::string& name);
}
