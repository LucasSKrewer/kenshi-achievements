#include "SaveFile.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace SaveFile
{
	namespace
	{
		// Leitura sequencial com checagem de limite: arquivo truncado ou formato inesperado vira erro, não
		// leitura fora do buffer.
		struct Reader
		{
			const std::vector<char>& b;
			size_t p;
			bool ok;
			Reader(const std::vector<char>& buf, size_t pos) : b(buf), p(pos), ok(true) {}

			int i()
			{
				if (!ok || p + 4 > b.size()) { ok = false; return 0; }
				int v;
				memcpy(&v, &b[p], 4);
				p += 4;
				return v;
			}
			std::string s()
			{
				int n = i();
				if (!ok || n < 0 || p + (size_t)n > b.size()) { ok = false; return std::string(); }
				std::string v(b.begin() + p, b.begin() + p + n);
				p += n;
				return v;
			}
			void skip(size_t n)
			{
				if (!ok || p + n > b.size()) { ok = false; return; }
				p += n;
			}
			// Quantidade de itens de uma lista: negativa ou absurda = formato errado
			int count()
			{
				int n = i();
				if (n < 0 || n > 1000000) ok = false;
				return ok ? n : 0;
			}
		};
	}

	// Formato do GameData no save (o mesmo dos .mod): int tipo, int id, str nome, str SID, int flags, e as
	// listas bool, float, int, vec3, vec4, string (cada uma: int quantidade + chave/valor).
	bool readRecord(const std::string& path, int type, const std::string& sid,
		std::map<std::string, int>& ints, std::map<std::string, std::string>& strs, std::string& error)
	{
		std::ifstream in(path.c_str(), std::ios::binary);
		if (!in)
		{
			error = "não abriu " + path;
			return false;
		}
		std::vector<char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

		// O SID vem com o tamanho na frente: procura os 4 bytes de tamanho + o texto
		std::string needle(4, '\0');
		int len = (int)sid.size();
		memcpy(&needle[0], &len, 4);
		needle += sid;
		std::vector<char>::iterator hit = b.begin();
		while (true)
		{
			hit = std::search(hit, b.end(), needle.begin(), needle.end());
			if (hit == b.end())
			{
				error = "registro " + sid + " não está no save";
				return false;
			}
			size_t sidAt = hit - b.begin();
			// Volta pro tipo: tipo, id, nome (tamanho + texto), e confere se é mesmo o registro
			for (size_t nameLen = 0; nameLen < 256 && sidAt >= 12 + nameLen; ++nameLen)
			{
				size_t start = sidAt - 12 - nameLen;
				Reader r(b, start);
				int t = r.i();
				r.i(); // id
				std::string name = r.s();
				if (!r.ok || t != type || name.size() != nameLen || r.p != sidAt)
					continue;
				r.s(); // SID
				r.i(); // flags
				int n = r.count();
				for (int k = 0; k < n && r.ok; ++k) { r.s(); r.skip(1); }   // bool
				n = r.count();
				for (int k = 0; k < n && r.ok; ++k) { r.s(); r.skip(4); }   // float
				n = r.count();
				std::map<std::string, int> gotInts;
				for (int k = 0; k < n && r.ok; ++k) { std::string key = r.s(); gotInts[key] = r.i(); }
				n = r.count();
				for (int k = 0; k < n && r.ok; ++k) { r.s(); r.skip(12); }  // vec3
				n = r.count();
				for (int k = 0; k < n && r.ok; ++k) { r.s(); r.skip(16); }  // vec4
				n = r.count();
				std::map<std::string, std::string> gotStrs;
				for (int k = 0; k < n && r.ok; ++k) { std::string key = r.s(); gotStrs[key] = r.s(); }
				if (!r.ok)
				{
					error = "registro " + sid + " truncado ou em formato desconhecido";
					return false;
				}
				ints.swap(gotInts);
				strs.swap(gotStrs);
				return true;
			}
			++hit; // o texto apareceu em outro lugar (não é o registro): continua procurando
		}
	}
}
