#include "Stats.h"
#include "Lang.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sstream>

namespace Stats
{
	namespace
	{
		std::map<std::string, CharStats> chars; // chave = handle
		std::map<std::string, int> killsByRace, kosByRace, killsByFaction, kosByFaction;
		std::map<std::string, int> killsByNpc, kosByNpc; // só NPCs únicos
		int totalKills = 0;
		int totalKOs = 0;
		int totalStealthKOs = 0;
		int totalLimbs = 0;

		// Sequências: instantes (relógio do jogo, em segundos) das últimas kills/derrubadas do grupo,
		// e o recorde por tamanho de janela (em segundos).
		std::deque<double> killTimes, takedownTimes;
		std::map<int, int> bestBurstKills, bestBurstTakedowns;
		const double BURST_KEEP_SECONDS = 600.0;

		std::vector<Achievement> achievements;
		std::map<std::string, std::string> settings;
		std::set<std::string> unlocked;
		std::vector<Unlock> pending;

		const char* const P_CHAR_KILLS = "c.k:";
		const char* const P_CHAR_KOS = "c.o:";
		const char* const P_CHAR_STEALTH = "c.s:";
		const char* const P_CHAR_LIMBS = "c.v:"; // "c.l:" foi usada numa versão de teste que contava errado
		const char* const P_CHAR_NAME = "c.n:";
		const char* const P_CHAR_RACE = "c.r:";
		const char* const P_RACE_KILLS = "r.k:";
		const char* const P_RACE_KOS = "r.o:";
		const char* const P_FACTION_KILLS = "f.k:";
		const char* const P_FACTION_KOS = "f.o:";
		const char* const P_NPC_KILLS = "n.k:";
		const char* const P_NPC_KOS = "n.o:";
		const char* const P_BURST_KILLS = "b.k:";
		const char* const P_BURST_TAKEDOWNS = "b.t:";
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

		bool hasPrefix(const std::string& s, const char* prefix)
		{
			std::string p(prefix);
			return s.compare(0, p.size(), p) == 0;
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

		// Vírgula separa alternativas: "Dust Bandits,Hungry Bandits" ou "*Skeleton*,Soldierbot".
		std::vector<std::string> splitAlternatives(const std::string& pattern)
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
			return alts;
		}

		bool anyNameMatches(const std::string& pattern, const std::set<std::string>& names)
		{
			std::vector<std::string> alts = splitAlternatives(pattern);
			for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
				for (size_t i = 0; i < alts.size(); ++i)
					if (globMatch(alts[i].c_str(), it->c_str()))
						return true;
			return false;
		}

		// Soma as entradas cujo nome casa com o padrão.
		int lookup(const std::map<std::string, int>& m, const std::string& pattern)
		{
			std::vector<std::string> alts = splitAlternatives(pattern);
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

		int charField(const CharStats& c, const std::string& metric)
		{
			if (metric == "char_kills") return c.kills;
			if (metric == "char_kos") return c.kos;
			if (metric == "char_stealth_kos") return c.stealthKos;
			if (metric == "char_limbs") return c.limbs;
			return 0;
		}

		int bestOf(const std::map<int, int>& m, const std::string& arg)
		{
			std::map<int, int>::const_iterator it = m.find(atoi(arg.c_str()));
			return it == m.end() ? 0 : it->second;
		}

		// Valor atual da métrica. Para char_*, `who` recebe o nome do melhor personagem.
		int metricValue(const Achievement& a, std::string& who)
		{
			who.clear();
			const std::string& m = a.metric;
			if (m == "kills") return totalKills;
			if (m == "kos") return totalKOs;
			if (m == "takedowns") return totalKills + totalKOs;
			if (m == "limbs") return totalLimbs;
			if (m == "stealth_kos") return totalStealthKOs;
			if (m == "race_kills") return lookup(killsByRace, a.arg);
			if (m == "race_kos") return lookup(kosByRace, a.arg);
			if (m == "faction_kills") return lookup(killsByFaction, a.arg);
			if (m == "faction_kos") return lookup(kosByFaction, a.arg);
			if (m == "npc_kills") return lookup(killsByNpc, a.arg);
			if (m == "npc_kos") return lookup(kosByNpc, a.arg);
			if (m == "npc_takedowns") return lookup(killsByNpc, a.arg) + lookup(kosByNpc, a.arg); // derrotar = matar ou nocautear
			if (m == "burst_kills") return bestOf(bestBurstKills, a.arg);
			if (m == "burst_takedowns") return bestOf(bestBurstTakedowns, a.arg);
			if (hasPrefix(m, "char_"))
			{
				int best = 0;
				for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
				{
					int v = charField(it->second, m);
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
			return m == "kills" || m == "kos" || m == "takedowns" || m == "limbs" || m == "stealth_kos"
				|| m == "char_kills" || m == "char_kos" || m == "char_limbs" || m == "char_stealth_kos"
				|| m == "race_kills" || m == "race_kos"
				|| m == "faction_kills" || m == "faction_kos"
				|| m == "npc_kills" || m == "npc_kos" || m == "npc_takedowns"
				|| m == "burst_kills" || m == "burst_takedowns";
		}

		bool needsArg(const std::string& m)
		{
			return hasPrefix(m, "race_") || hasPrefix(m, "faction_") || hasPrefix(m, "npc_") || hasPrefix(m, "burst_");
		}

		// Atualiza o recorde de "N em X segundos" pra cada janela citada por alguma conquista.
		void updateBursts(std::deque<double>& times, std::map<int, int>& best, const char* metric, double now)
		{
			if (!times.empty() && now < times.back())
				times.clear(); // o relógio voltou (outro save): descarta a sequência
			times.push_back(now);
			while (!times.empty() && times.front() < now - BURST_KEEP_SECONDS)
				times.pop_front();
			for (size_t i = 0; i < achievements.size(); ++i)
			{
				if (achievements[i].metric != metric)
					continue;
				int window = atoi(achievements[i].arg.c_str());
				if (window <= 0)
					continue;
				int n = 0;
				for (std::deque<double>::const_reverse_iterator t = times.rbegin(); t != times.rend() && *t >= now - window; ++t)
					++n;
				if (n > best[window])
					best[window] = n;
			}
		}

		void record(bool kill, const Takedown& t)
		{
			CharStats& c = chars[t.key];
			c.name = t.name;
			if (!t.charRace.empty())
				c.race = t.charRace;
			if (kill)
			{
				++c.kills;
				++totalKills;
				if (!t.race.empty()) ++killsByRace[t.race];
				if (!t.faction.empty()) ++killsByFaction[t.faction];
				if (!t.npc.empty()) ++killsByNpc[t.npc];
			}
			else
			{
				++c.kos;
				++totalKOs;
				if (t.stealth)
				{
					++c.stealthKos;
					++totalStealthKOs;
				}
				if (!t.race.empty()) ++kosByRace[t.race];
				if (!t.faction.empty()) ++kosByFaction[t.faction];
				if (!t.npc.empty()) ++kosByNpc[t.npc];
			}
			if (t.time >= 0)
			{
				if (kill)
					updateBursts(killTimes, bestBurstKills, "burst_kills", t.time);
				updateBursts(takedownTimes, bestBurstTakedowns, "burst_takedowns", t.time);
			}
			checkAchievements(true);
		}

		bool byKillsDesc(const CharStats& a, const CharStats& b)
		{
			if (a.kills != b.kills) return a.kills > b.kills;
			if (a.kos != b.kos) return a.kos > b.kos;
			return a.limbs > b.limbs;
		}

		bool byKillsDescPair(const std::pair<CharStats, std::string>& a, const std::pair<CharStats, std::string>& b)
		{
			return byKillsDesc(a.first, b.first);
		}

		bool byCountDesc(const std::pair<int, std::string>& a, const std::pair<int, std::string>& b)
		{
			if (a.first != b.first) return a.first > b.first;
			return a.second < b.second;
		}
	}

	// Formato: id | métrica[:argumento] | alvo | título | descrição
	//   # comentário      @chave = valor      [Categoria]      ?id = secreta
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
		std::string line, category;
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

			if (line[0] == '[' && line[line.size() - 1] == ']')
			{
				category = trim(line.substr(1, line.size() - 2));
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
			a.category = category;
			a.id = f[0];
			if (!a.id.empty() && a.id[0] == '?')
			{
				a.secret = true;
				a.id = trim(a.id.substr(1)); // o id salvo no save não leva o '?'
			}
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
				errors.push_back(where + a.metric + " needs an argument (e.g. race_kills:Shek, burst_kills:30)");
			else if (hasPrefix(a.metric, "burst_") && atoi(a.arg.c_str()) <= 0)
				errors.push_back(where + a.metric + " needs a time window in seconds (e.g. " + a.metric + ":30)");
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
		killsByNpc.clear();
		kosByNpc.clear();
		totalKills = 0;
		totalKOs = 0;
		totalStealthKOs = 0;
		totalLimbs = 0;
		killTimes.clear();
		takedownTimes.clear();
		bestBurstKills.clear();
		bestBurstTakedowns.clear();
		unlocked.clear();
		pending.clear();
	}

	void recordKill(const Takedown& t)
	{
		record(true, t);
	}

	void recordKO(const Takedown& t)
	{
		record(false, t);
	}

	void recordLimb(const std::string& key, const std::string& name, const std::string& charRace)
	{
		CharStats& c = chars[key];
		c.name = name;
		if (!charRace.empty())
			c.race = charRace;
		++c.limbs;
		++totalLimbs;
		checkAchievements(true);
	}

	namespace
	{
		Takedown simple(const std::string& key, const std::string& name, const std::string& charRace,
			const std::string& race, const std::string& faction)
		{
			Takedown t;
			t.key = key;
			t.name = name;
			t.charRace = charRace;
			t.race = race;
			t.faction = faction;
			return t;
		}
	}

	void recordKill(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction)
	{
		record(true, simple(key, name, charRace, race, faction));
	}

	void recordKO(const std::string& key, const std::string& name, const std::string& charRace,
		const std::string& race, const std::string& faction)
	{
		record(false, simple(key, name, charRace, race, faction));
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
		if (totalStealthKOs) ints["total.s"] = totalStealthKOs;
		if (totalLimbs) ints["total.v"] = totalLimbs;
		for (std::map<std::string, CharStats>::const_iterator it = chars.begin(); it != chars.end(); ++it)
		{
			ints[P_CHAR_KILLS + it->first] = it->second.kills;
			ints[P_CHAR_KOS + it->first] = it->second.kos;
			if (it->second.stealthKos) ints[P_CHAR_STEALTH + it->first] = it->second.stealthKos;
			if (it->second.limbs) ints[P_CHAR_LIMBS + it->first] = it->second.limbs;
			strs[P_CHAR_NAME + it->first] = it->second.name;
			if (!it->second.race.empty())
				strs[P_CHAR_RACE + it->first] = it->second.race;
		}
		std::map<std::string, int>::const_iterator i;
		for (i = killsByRace.begin(); i != killsByRace.end(); ++i) ints[P_RACE_KILLS + i->first] = i->second;
		for (i = kosByRace.begin(); i != kosByRace.end(); ++i) ints[P_RACE_KOS + i->first] = i->second;
		for (i = killsByFaction.begin(); i != killsByFaction.end(); ++i) ints[P_FACTION_KILLS + i->first] = i->second;
		for (i = kosByFaction.begin(); i != kosByFaction.end(); ++i) ints[P_FACTION_KOS + i->first] = i->second;
		for (i = killsByNpc.begin(); i != killsByNpc.end(); ++i) ints[P_NPC_KILLS + i->first] = i->second;
		for (i = kosByNpc.begin(); i != kosByNpc.end(); ++i) ints[P_NPC_KOS + i->first] = i->second;
		std::map<int, int>::const_iterator b;
		for (b = bestBurstKills.begin(); b != bestBurstKills.end(); ++b) ints[P_BURST_KILLS + itos(b->first)] = b->second;
		for (b = bestBurstTakedowns.begin(); b != bestBurstTakedowns.end(); ++b) ints[P_BURST_TAKEDOWNS + itos(b->first)] = b->second;
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
			else if (k == "total.s") totalStealthKOs = v;
			else if (k == "total.v") totalLimbs = v;
			else if (startsWith(k, P_CHAR_KILLS, rest)) chars[rest].kills = v;
			else if (startsWith(k, P_CHAR_KOS, rest)) chars[rest].kos = v;
			else if (startsWith(k, P_CHAR_STEALTH, rest)) chars[rest].stealthKos = v;
			else if (startsWith(k, P_CHAR_LIMBS, rest)) chars[rest].limbs = v;
			// Saves antigos podem ter nomes traduzidos ("Bandidos da Poeira"): leva pro inglês e soma
			else if (startsWith(k, P_RACE_KILLS, rest)) killsByRace[Lang::toEnglish(rest)] += v;
			else if (startsWith(k, P_RACE_KOS, rest)) kosByRace[Lang::toEnglish(rest)] += v;
			else if (startsWith(k, P_FACTION_KILLS, rest)) killsByFaction[Lang::toEnglish(rest)] += v;
			else if (startsWith(k, P_FACTION_KOS, rest)) kosByFaction[Lang::toEnglish(rest)] += v;
			else if (startsWith(k, P_NPC_KILLS, rest)) killsByNpc[rest] += v;
			else if (startsWith(k, P_NPC_KOS, rest)) kosByNpc[rest] += v;
			else if (startsWith(k, P_BURST_KILLS, rest)) bestBurstKills[atoi(rest.c_str())] = v;
			else if (startsWith(k, P_BURST_TAKEDOWNS, rest)) bestBurstTakedowns[atoi(rest.c_str())] = v;
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
		std::string statLine(const CharStats& c)
		{
			std::string s = Lang::tr("stats.line", "Kills: {kills}    KOs: {kos}    Limbs: {limbs}");
			s = Lang::fill(s, "kills", c.kills);
			s = Lang::fill(s, "kos", c.kos);
			s = Lang::fill(s, "limbs", c.limbs);
			return s;
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

		// "A 40, B 35, C 21" com os `n` maiores.
		std::string topOf(const std::map<std::string, int>& m, size_t n)
		{
			std::vector<std::pair<int, std::string> > v;
			for (std::map<std::string, int>::const_iterator it = m.begin(); it != m.end(); ++it)
				v.push_back(std::make_pair(it->second, it->first));
			std::sort(v.begin(), v.end(), byCountDesc);
			std::ostringstream o;
			for (size_t i = 0; i < v.size() && i < n; ++i)
				o << (i ? ", " : "") << v[i].second << " " << v[i].first;
			return o.str();
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
			o << "   " << statLine(sel) << "\n";
			if (sel.stealthKos)
				o << "   " << Lang::fill(Lang::tr("stats.stealth", "Stealth knockouts: {n}"), "n", sel.stealthKos) << "\n";
		}

		CharStats total;
		total.kills = totalKills;
		total.kos = totalKOs;
		total.limbs = totalLimbs;
		o << "\n" << Lang::tr("stats.squad", "--- Squad total ---") << "\n";
		o << "   " << statLine(total) << "\n";
		if (totalStealthKOs)
			o << "   " << Lang::fill(Lang::tr("stats.stealth", "Stealth knockouts: {n}"), "n", totalStealthKOs) << "\n";

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
			o << displayName(list[i].second, c.name, c.race) << ":  " << statLine(c) << "\n";
		}

		if (!killsByRace.empty() || !killsByFaction.empty())
		{
			o << "\n" << Lang::tr("stats.victims", "--- Most frequent victims ---") << "\n";
			if (!killsByRace.empty())
				o << "   " << Lang::tr("stats.victims_race", "Races:") << " " << topOf(killsByRace, 5) << "\n";
			if (!killsByFaction.empty())
				o << "   " << Lang::tr("stats.victims_faction", "Factions:") << " " << topOf(killsByFaction, 5) << "\n";
		}
		return o.str();
	}

	std::vector<std::string> setKnownNames(const std::set<std::string>& races, const std::set<std::string>& factions,
		const std::set<std::string>& npcs)
	{
		std::vector<std::string> hidden;
		for (size_t i = 0; i < achievements.size(); ++i)
		{
			Achievement& a = achievements[i];
			if (hasPrefix(a.metric, "race_"))
				a.available = races.empty() || anyNameMatches(a.arg, races);
			else if (hasPrefix(a.metric, "faction_"))
				a.available = factions.empty() || anyNameMatches(a.arg, factions);
			else if (hasPrefix(a.metric, "npc_"))
				a.available = npcs.empty() || anyNameMatches(a.arg, npcs);
			else
				a.available = true;
			if (!a.available)
				hidden.push_back(a.id);
		}
		return hidden;
	}

	namespace
	{
		// Visível = existe no jogo carregado, ou já foi liberada neste save (continua aparecendo
		// mesmo se o mod de origem saiu depois).
		bool visible(const Achievement& a)
		{
			return a.available || unlocked.count(a.id) != 0;
		}

		void writeAchievement(std::ostringstream& o, const Achievement& a)
		{
			if (unlocked.count(a.id))
			{
				o << "[X] " << title(a) << "\n";
			}
			else if (a.secret)
			{
				// Secreta: nada de título, descrição ou progresso até liberar
				o << "[?] ???  " << Lang::tr("ach.secret", "(secret achievement)") << "\n";
				return;
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
	}

	std::string achievementsReport()
	{
		int total = 0, done = 0;
		std::vector<std::string> categories; // na ordem em que aparecem no arquivo
		for (size_t i = 0; i < achievements.size(); ++i)
		{
			const Achievement& a = achievements[i];
			if (!visible(a))
				continue;
			++total;
			if (unlocked.count(a.id))
				++done;
			if (std::find(categories.begin(), categories.end(), a.category) == categories.end())
				categories.push_back(a.category);
		}

		std::ostringstream o;
		std::string summary = Lang::tr("ach.summary", "Completed: {done}/{total}");
		summary = Lang::fill(summary, "done", done);
		summary = Lang::fill(summary, "total", total);
		o << summary << "  (" << (total ? done * 100 / total : 0) << "%)\n";

		// Por categoria; dentro de cada uma, concluídas primeiro e depois as pendentes
		for (size_t c = 0; c < categories.size(); ++c)
		{
			o << "\n";
			if (!categories[c].empty())
				o << "--- " << Lang::tr("cat." + categories[c], categories[c]) << " ---\n";
			for (int pass = 0; pass < 2; ++pass)
			{
				for (size_t i = 0; i < achievements.size(); ++i)
				{
					const Achievement& a = achievements[i];
					if (a.category != categories[c] || !visible(a))
						continue;
					if ((unlocked.count(a.id) != 0) != (pass == 0))
						continue;
					writeAchievement(o, a);
				}
			}
		}
		return o.str();
	}
}
