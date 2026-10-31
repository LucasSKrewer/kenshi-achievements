#pragma once

// Lê o registro do mod direto de um quick.save no disco, sem o jogo (#37): o Import do Kenshi cria um
// mundo novo e não traz o registro do save importado, então o plugin busca os contadores no arquivo.
// Não depende de nada do Kenshi, então dá pra testar fora do jogo.
// Compila no VS2010 (v100): nada de range-for, initializer list ou enum class.

#include <map>
#include <string>

namespace SaveFile
{
	// Procura o GameData de tipo `type` e SID `sid` em `path` e devolve os campos inteiros e de texto.
	// false (com o motivo em `error`) se o arquivo não abrir, o registro não existir ou estiver truncado.
	bool readRecord(const std::string& path, int type, const std::string& sid,
		std::map<std::string, int>& ints, std::map<std::string, std::string>& strs, std::string& error);
}
