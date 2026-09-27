#include "Stats.h"
#include "Lang.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Stats
{
	namespace
	{
		std::map<std::string, CharStats> chars; // chave = handle
		std::map<std::string, int> killsByRace, kosByRace, killsByFaction, kosByFaction;
		int totalKills = 0;
		int totalKOs = 0;

		std::vector<Achievement> achievements;
		std::map<std::string, std::string> settings;
		std::set<std::string> unlocked;
		std::vector<Unlock> pending;

		const char* const P_CHAR_KILLS = "c.k:";
		const char* const P_CHAR_KOS = "c.o:";
		const char* const P_CHAR_NAME = "c.n:";
		const char* const P_CHAR_RACE = "c.r:";
		const char* const P_RACE_KILLS = "r.k:";
		const char* const P_RACE_KOS = "r.o:";
		const char* const P_FACTION_KILLS = "f.k:";
		const char* const P_FACTION_KOS = "f.o:";
		const char* const P_UNLOCKED = "a:";

		std::string trim(const std::string& s)
		{
			size_t b = s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos)
				return "";
			size_t e = s.find_last_not_of(" \t\r\n");
			return s.substr(b, e - b + 1);
		}

		std::string itos(int v)
		{
			std::ostringstream o;
			o << v;
			return o.str();
		}

		bool startsWith(const std::string& s, const char* prefix, std::string& rest)
		{
			std::string p(prefix);
			if (s.compare(0, p.size(), p) != 0)
				return false;
			rest = s.substr(p.size());
			return true;
		}

		// Comparação sem diferenciar maiúsculas, com '*' casando qualquer trecho.
		// Mods como o Genesis criam variantes (Skeleton MKI, MKII...), então "Skeleton*" pega todas.
		bool globMatch(const char* pat, const char* s)
		{
			if (*pat == '\0')
				return *s == '\0';
			if (*pat == '*')
				return globMatch(pat + 1, s) || (*s != '\0' && globMatch(pat, s + 1));
			return *s != '\0' && tolower((unsigned char)*pat) == tolower((unsigned char)*s) && globMatch(pat + 1, s + 1);
		}

		// Soma as entradas cujo nome casa com o padrão. Vírgula separa alternativas:
		// "Dust Bandits,Bandidos da Poeira" ou "*Skeleton*,Soldierbot".
		int lookup(const std::map<std::string, int>& m, const std::string& pattern)
		{
			std::vector<std::string> alts;
			std::stringstream ss(pattern);
			std::string part;
			while (std::getline(ss, part, ','))
			{
				part = trim(part);
				if (!part.empty())
					alts.push_back(part);
			}
			int total = 0;
			for (std::map<std::string, int>::const_iterator it = m.begin(); it != m.end(); ++it)
			{
				for (size_t i = 0; i < alts.size(); ++i)
				{
					if (globMatch(alts[i].c_str(), it->first.c_str()))
					{
						total += it->second;
						break;
					}
				}
			}
			return total;
		}

		std::string title(const Achievement& a)
		{
			return Lang::tr(a.id + ".title", a.title);
		}

		std::string description(const Achievement& a)
		{
			return Lang::tr(a.id + ".desc", a.description);
		}

		// Valor atual da métrica. Para char_*, `who` recebe o nome do melhor personagem.
		int metricValue(const Achievement& a, std::string& who)
		{
			who.clear();
			if (a.metric == "kills") return totalKills;
			if (a.metric == "kos") return totalKOs;
			if (a.metric == "takedowns") return totalKills + totalKOs;
			if (a.metric == "race_kills") return lookup(killsByRace, a.arg);
			if (a.metric == "race_kos") return lookup(kosByRace, a.arg);
			if (a.metric == "faction_kills") return lookup(killsByFaction, a.arg);
			if (a.metric == "faction_kos") return lookup(kosByFaction, a.arg);
			if (a.metric == "char_kills" || a.metric == "char_kos")
			{
				bool k = a.metric == "char_kills";
				int best = 0;
				for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
				{
					int v = k ? it->second.kills : it->second.kos;
					if (v > best)
					{
						best = v;
						who = it->second.name;
					}
				}
				return best;
			}
			return 0;
		}

		void checkAchievements(bool announce)
		{
			for (size_t i = 0; i < achievements.size(); ++i)
			{
				const Achievement& a = achievements[i];
				if (unlocked.count(a.id))
					continue;
				std::string who;
				if (metricValue(a, who) >= a.target)
				{
					unlocked.insert(a.id);
					if (announce)
					{
						Unlock u;
						u.title = title(a);
						u.description = description(a);
						u.who = who;
						pending.push_back(u);
					}
				}
			}
		}

		bool isKnownMetric(const std::string& m)
		{
			return m == "kills" || m == "kos" || m == "takedowns"
				|| m == "char_kills" || m == "char_kos"
				|| m == "race_kills" || m == "race_kos"
				|| m == "faction_kills" || m == "faction_kos";
		}

		bool needsArg(const std::string& m)
		{
			return m.compare(0, 5, "race_") == 0 || m.compare(0, 8, "faction_") == 0;
		}

		void record(bool kill, const std::string& key, const std::string& name, const std::string& charRace,
			const std::string& race, const std::string& faction)
		{
			CharStats& c = chars[key];
			c.name = name;
			if (!charRace.empty())
				c.race = charRace;
			if (kill)
			{
				++c.kills;
				++totalKills;
				if (!race.empty()) ++killsByRace[race];
				if (!faction.empty()) ++killsByFaction[faction];
			}
			else
			{
				++c.kos;
				++totalKOs;
				if (!race.empty()) ++kosByRace[race];
				if (!faction.empty()) ++kosByFaction[faction];
			}
			checkAchievements(true);
		}

		bool byKillsDesc(const CharStats& a, const CharStats& b)
		{
			if (a.kills != b.kills) return a.kills > b.kills;
			return a.kos > b.kos;
		}

		bool byKillsDescPair(const std::pair<CharStats, std::string>& a, const std::pair<CharStats, std::string>& b)
		{
			return byKillsDesc(a.first, b.first);
		}
	}

	// Formato: id | métrica[:argumento] | alvo | título | descrição   (# = comentário)
	int loadAchievements(const std::string& path, std::vector<std::string>& errors)
	{
		achievements.clear();
		settings.clear();
		std::ifstream in(path.c_str());
		if (!in)
		{
			errors.push_back("could not open " + path);
			return 0;
		}

		std::set<std::string> ids;
		std::string line;
		int lineNo = 0;
		while (std::getline(in, line))
		{
			++lineNo;
			// BOM UTF-8
			if (lineNo == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
				line = line.substr(3);
			line = trim(line);
			if (line.empty() || line[0] == '#')
				continue;

			if (line[0] == '@')
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					errors.push_back("line " + itos(lineNo) + ": expected @key = value");
				else
					settings[trim(line.substr(1, eq - 1))] = trim(line.substr(eq + 1));
				continue;
			}

			std::vector<std::string> f;
			std::stringstream ss(line);
			std::string part;
			while (std::getline(ss, part, '|'))
				f.push_back(trim(part));

			std::string where = "line " + itos(lineNo) + ": ";
			if (f.size() < 4)
			{
				errors.push_back(where + "expected id | metric | target | title | description");
				continue;
			}

			Achievement a;
			a.id = f[0];
			std::string metric = f[1];
			size_t colon = metric.find(':');
			if (colon != std::string::npos)
			{
				a.arg = trim(metric.substr(colon + 1));
				metric = trim(metric.substr(0, colon));
			}
			a.metric = metric;
			a.target = atoi(f[2].c_str());
			a.title = f[3];
			a.description = f.size() > 4 ? f[4] : "";

			if (!isKnownMetric(a.metric))
				errors.push_back(where + "unknown metric '" + a.metric + "'");
			else if (needsArg(a.metric) && a.arg.empty())
				errors.push_back(where + a.metric + " needs an argument (e.g. " + a.metric + ":Shek)");
			else if (a.target <= 0)
				errors.push_back(where + "target must be > 0");
			else if (!ids.insert(a.id).second)
				errors.push_back(where + "duplicate id '" + a.id + "'");
			else
				achievements.push_back(a);
		}
		return (int)achievements.size();
	}

	std::string setting(const std::string& key, const std::string& fallback)
	{
		std::map<std::string, std::string>::const_iterator it = settings.find(key);
		return it == settings.end() || it->second.empty() ? fallback : it->second;
	}

	void reset()
	{
		chars.clear();
		killsByRace.clear();
		kosByRace.clear();
		killsByFaction.clear();
		kosByFaction.clear();
		totalKills = 0;
		totalKOs = 0;
		unlocked.clear();
		pending.clear();
	}

	void recordKill(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction)
	{
		record(true, key, name, charRace, race, faction);
	}

	void recordKO(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction)
	{
		record(false, key, name, charRace, race, faction);
	}

	std::vector<Unlock> popUnlocks()
	{
		std::vector<Unlock> out;
		out.swap(pending);
		return out;
	}

	void exportTo(std::map<std::string, int>& ints, std::map<std::string, std::string>& strs)
	{
		ints["total.k"] = totalKills;
		ints["total.o"] = totalKOs;
		for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
		{
			ints[P_CHAR_KILLS + it->first] = it->second.kills;
			ints[P_CHAR_KOS + it->first] = it->second.kos;
			strs[P_CHAR_NAME + it->first] = it->second.name;
			if (!it->second.race.empty())
				strs[P_CHAR_RACE + it->first] = it->second.race;
		}
		std::map<std::string, int>::const_iterator i;
		for (i = killsByRace.begin(); i != killsByRace.end(); ++i) ints[P_RACE_KILLS + i->first] = i->second;
		for (i = kosByRace.begin(); i != kosByRace.end(); ++i) ints[P_RACE_KOS + i->first] = i->second;
		for (i = killsByFaction.begin(); i != killsByFaction.end(); ++i) ints[P_FACTION_KILLS + i->first] = i->second;
		for (i = kosByFaction.begin(); i != kosByFaction.end(); ++i) ints[P_FACTION_KOS + i->first] = i->second;
		for (std::set<std::string>::const_iterator a = unlocked.begin(); a != unlocked.end(); ++a)
			ints[P_UNLOCKED + *a] = 1;
	}

	void importFrom(const std::map<std::string, int>& ints, const std::map<std::string, std::string>& strs)
	{
		reset();
		for (std::map<std::string, int>::const_iterator it = ints.begin(); it != ints.end(); ++it)
		{
			const std::string& k = it->first;
			int v = it->second;
			std::string rest;
			if (k == "total.k") totalKills = v;
			else if (k == "total.o") totalKOs = v;
			else if (startsWith(k, P_CHAR_KILLS, rest)) chars[rest].kills = v;
			else if (startsWith(k, P_CHAR_KOS, rest)) chars[rest].kos = v;
			else if (startsWith(k, P_RACE_KILLS, rest)) killsByRace[rest] = v;
			else if (startsWith(k, P_RACE_KOS, rest)) kosByRace[rest] = v;
			else if (startsWith(k, P_FACTION_KILLS, rest)) killsByFaction[rest] = v;
			else if (startsWith(k, P_FACTION_KOS, rest)) kosByFaction[rest] = v;
			else if (startsWith(k, P_UNLOCKED, rest)) unlocked.insert(rest);
		}
		for (std::map<std::string, std::string>::const_iterator it = strs.begin(); it != strs.end(); ++it)
		{
			std::string rest;
			if (startsWith(it->first, P_CHAR_NAME, rest))
				chars[rest].name = it->second;
			else if (startsWith(it->first, P_CHAR_RACE, rest))
				chars[rest].race = it->second;
		}
		// Conquistas novas no achievements.txt que o save antigo já cumpre: libera sem anunciar.
		checkAchievements(false);
	}

	namespace
	{
		std::string statLine(int kills, int kos)
		{
			return Lang::fill(Lang::fill(Lang::tr("stats.line", "Kills: {kills}    KOs: {kos}"), "kills", kills), "kos", kos);
		}

		// Nome pra exibir; se outro personagem tem o mesmo nome, acrescenta a raça pra diferenciar.
		std::string displayName(const std::string& key, const std::string& name, const std::string& race)
		{
			if (race.empty())
				return name;
			for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
			{
				if (it->first != key && it->second.name == name)
					return name + " (" + race + ")";
			}
			return name;
		}
	}

	std::string statsReport(const std::string& selectedKey, const std::string& selectedName)
	{
		std::ostringstream o;
		if (selectedName.empty())
		{
			o << Lang::tr("stats.no_selection", "(no character selected)") << "\n";
		}
		else
		{
			CharStats sel;
			std::map<std::string, CharStats>::const_iterator it = chars.find(selectedKey);
			if (it != chars.end())
				sel = it->second;
			o << displayName(selectedKey, selectedName, sel.race) << "\n";
			o << "   " << statLine(sel.kills, sel.kos) << "\n";
		}

		o << "\n" << Lang::tr("stats.squad", "--- Squad total ---") << "\n";
		o << "   " << statLine(totalKills, totalKOs) << "\n";

		// Lista com a chave junto, pra desambiguar homônimos
		std::vector<std::pair<CharStats, std::string> > list;
		for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
			list.push_back(std::make_pair(it->second, it->first));
		std::sort(list.begin(), list.end(), byKillsDescPair);

		o << "\n" << Lang::tr("stats.per_char", "--- Per character ---") << "\n";
		if (list.empty())
			o << Lang::tr("stats.none", "(nobody yet)") << "\n";
		for (size_t i = 0; i < list.size(); ++i)
		{
			const CharStats& c = list[i].first;
			o << displayName(list[i].second, c.name, c.race) << ":  " << statLine(c.kills, c.kos) << "\n";
		}
		return o.str();
	}

	std::string achievementsReport()
	{
		std::ostringstream o;
		o << Lang::fill(Lang::fill(Lang::tr("ach.summary", "Completed: {done}/{total}"),
			"done", (int)unlocked.size()), "total", (int)achievements.size()) << "\n\n";

		// Concluídas primeiro, depois as pendentes com progresso
		for (int pass = 0; pass < 2; ++pass)
		{
			o << (pass == 0 ? Lang::tr("ach.done", "--- Completed ---") : Lang::tr("ach.pending", "--- In progress ---")) << "\n";
			int shown = 0;
			for (size_t i = 0; i < achievements.size(); ++i)
			{
				const Achievement& a = achievements[i];
				bool done = unlocked.count(a.id) != 0;
				if (done != (pass == 0))
					continue;
				++shown;
				if (done)
				{
					o << "[X] " << title(a) << "\n";
				}
				else
				{
					std::string who;
					int v = (std::min)(metricValue(a, who), a.target);
					o << "[ ] " << title(a) << "  (" << v << "/" << a.target << ")\n";
				}
				std::string d = description(a);
				if (!d.empty())
					o << "      " << d << "\n";
			}
			if (!shown)
				o << "   -\n";
			o << "\n";
		}
		return o.str();
	}
}
