#include "Lang.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace Lang
{
	namespace
	{
		std::map<std::string, std::string> texts;
		std::map<std::string, std::string> english; // nome traduzido -> nome em inglês

		std::string trim(const std::string& s)
		{
			size_t b = s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos)
				return "";
			size_t e = s.find_last_not_of(" \t\r\n");
			return s.substr(b, e - b + 1);
		}

		void stripBom(std::string& line)
		{
			if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
				line = line.substr(3);
		}

		// "chave = texto" por linha, '#' comenta, "\n" no texto vira quebra de linha.
		bool loadFile(const std::string& path)
		{
			std::ifstream in(path.c_str());
			if (!in)
				return false;
			std::string line;
			bool first = true;
			while (std::getline(in, line))
			{
				if (first)
				{
					stripBom(line);
					first = false;
				}
				line = trim(line);
				if (line.empty() || line[0] == '#')
					continue;
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string value = trim(line.substr(eq + 1));
				for (size_t p = value.find("\\n"); p != std::string::npos; p = value.find("\\n", p + 1))
					value.replace(p, 2, "\n");
				texts[trim(line.substr(0, eq))] = value;
			}
			return true;
		}

		// Conteúdo entre as primeiras aspas de uma linha .po, com \" e \\ resolvidos.
		std::string poString(const std::string& line)
		{
			size_t b = line.find('"');
			size_t e = line.rfind('"');
			if (b == std::string::npos || e <= b)
				return "";
			std::string raw = line.substr(b + 1, e - b - 1), out;
			for (size_t i = 0; i < raw.size(); ++i)
			{
				if (raw[i] == '\\' && i + 1 < raw.size())
					out += raw[++i] == 'n' ? '\n' : raw[i];
				else
					out += raw[i];
			}
			return out;
		}

		bool startsWith(const std::string& s, const char* p)
		{
			return s.compare(0, strlen(p), p) == 0;
		}

		// Remove marcas "/AF/", "/OA3/"... (barra, letras maiúsculas/dígitos, barra).
		std::string stripGenderTags(const std::string& s)
		{
			std::string out;
			for (size_t i = 0; i < s.size();)
			{
				if (s[i] == '/')
				{
					size_t j = i + 1;
					while (j < s.size() && (isupper((unsigned char)s[j]) || isdigit((unsigned char)s[j])))
						++j;
					if (j > i + 1 && j < s.size() && s[j] == '/')
					{
						i = j + 1; // pula a marca inteira
						continue;
					}
				}
				out += s[i++];
			}
			return out;
		}
	}

	std::string gameLanguage(const std::string& gameDir)
	{
		std::ifstream in((gameDir + "settings.cfg").c_str());
		std::string line;
		while (std::getline(in, line))
		{
			if (startsWith(line, "language="))
				return trim(line.substr(9));
		}
		return "";
	}

	std::string loadTexts(const std::string& modDir, const std::string& lang)
	{
		texts.clear();
		loadFile(modDir + "lang\\en.txt");
		if (lang.empty() || startsWith(lang, "en"))
			return "en";
		if (loadFile(modDir + "lang\\" + lang + ".txt"))
			return lang;
		if (lang.size() > 2 && loadFile(modDir + "lang\\" + lang.substr(0, 2) + ".txt"))
			return lang.substr(0, 2);
		return "en";
	}

	std::string tr(const std::string& key, const std::string& fallback)
	{
		std::map<std::string, std::string>::const_iterator it = texts.find(key);
		return it == texts.end() || it->second.empty() ? fallback : it->second;
	}

	std::string fill(const std::string& text, const char* name, const std::string& value)
	{
		std::string tag = std::string("{") + name + "}", out = text;
		for (size_t p = out.find(tag); p != std::string::npos; p = out.find(tag, p + value.size()))
			out.replace(p, tag.size(), value);
		return out;
	}

	std::string fill(const std::string& text, const char* name, int value)
	{
		std::ostringstream o;
		o << value;
		return fill(text, name, o.str());
	}

	// Entradas do .po:  msgctxt "RACE,Greenlander,"  msgid "Greenlander"  msgstr "Camponês"
	//
	// O Kenshi traduz o nome pelo texto em inglês, qualquer que seja o tipo: a raça "Leviathan" só tem
	// entrada ANIMAL_CHARACTER no .po e mesmo assim chega como "Leviatã". Por isso vale toda entrada
	// de NOME ("TIPO,Nome," com msgid == Nome; descrições têm um 3º campo), e RACE/FACTION têm
	// prioridade se o mesmo texto traduzido vier de mais de um nome.
	int loadGameNames(const std::string& poPath)
	{
		english.clear();
		std::ifstream in(poPath.c_str());
		if (!in)
			return 0;

		std::map<std::string, bool> fromRaceOrFaction; // tradução -> veio de RACE/FACTION
		std::string line, ctx, id, str, *current = NULL;
		int n = 0;
		for (bool more = true; more;)
		{
			more = (bool)std::getline(in, line);
			line = trim(line);
			bool startsEntry = !more || startsWith(line, "msgctxt");
			if (startsEntry && !ctx.empty())
			{
				// Fecha a entrada anterior
				size_t comma = ctx.find(',');
				bool isNameEntry = comma != std::string::npos && !id.empty()
					&& ctx.compare(comma + 1, std::string::npos, id + ",") == 0;
				if (isNameEntry && !str.empty() && str != id)
				{
					bool priority = startsWith(ctx, "RACE,") || startsWith(ctx, "FACTION,");
					std::map<std::string, std::string>::iterator e = english.find(str);
					if (e == english.end())
					{
						english[str] = id;
						fromRaceOrFaction[str] = priority;
						++n;
					}
					else if (priority && !fromRaceOrFaction[str])
					{
						e->second = id; // RACE/FACTION vence um nome de outro tipo
						fromRaceOrFaction[str] = true;
					}
					// Marcas de gênero do Kenshi ("Senhor/AF/ de Escravos"): o jogo pode mostrar o nome
					// já resolvido, então aprende também a forma sem a marca.
					std::string plain = stripGenderTags(str);
					if (plain != str && !plain.empty() && english.find(plain) == english.end())
						english[plain] = id;
				}
				ctx.clear();
				id.clear();
				str.clear();
				current = NULL;
			}
			if (!more)
				break;
			if (startsWith(line, "msgctxt"))
			{
				ctx = poString(line);
				current = &ctx;
			}
			else if (startsWith(line, "msgid"))
			{
				id = poString(line);
				current = &id;
			}
			else if (startsWith(line, "msgstr"))
			{
				str = poString(line);
				current = &str;
			}
			else if (!line.empty() && line[0] == '"' && current)
			{
				*current += poString(line); // continuação multilinha
			}
		}
		return n;
	}

	std::string toEnglish(const std::string& name)
	{
		std::map<std::string, std::string>::const_iterator it = english.find(name);
		return it == english.end() ? name : it->second;
	}
}
