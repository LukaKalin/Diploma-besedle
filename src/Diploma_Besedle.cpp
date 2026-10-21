#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <omp.h>
#include <mutex>
#include <cmath>

// ============================================================
// GLOBALNE SPREMENLJIVKE
// ============================================================

std::vector<std::string> g_ugibanja;
std::vector<std::string> g_kandidati;
std::map<std::string, int> g_ugibanja_idx;
std::map<std::string, int> g_kandidati_idx;
std::vector<std::vector<std::string>> g_tabela;
std::map<std::string, int> g_resitve_idx;

std::unordered_map<std::string, double> memo;
std::mutex memo_mutex;
long long memo_hits = 0;
long long compute_calls = 0;

// ============================================================
// UTF-8 PODPORA
// ============================================================

std::vector<uint32_t> utf8_v_unicode(const std::string& s) {
    std::vector<uint32_t> result;
    int i = 0;
    while (i < (int)s.size()) {
        unsigned char c = s[i];
        uint32_t cp;
        if (c < 0x80) {
            cp = c; i++;          // ASCII
        }
        else {
            cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); i += 2;  // 2-bajtni (č, š, ž)
        }
        result.push_back(cp);
    }
    return result;
}

int dolzina(const std::string& s) {
    return (int)utf8_v_unicode(s).size();
}

// ============================================================
// BRANJE DATOTEK
// ============================================================

std::vector<std::string> nalozi_besede(const std::string& pot) {
    std::vector<std::string> besede;
    std::ifstream datoteka(pot);
    std::string beseda;
    while (std::getline(datoteka, beseda)) {
        if (!beseda.empty() && beseda.back() == '\r')
            beseda.pop_back();
        if (!beseda.empty())
            besede.push_back(beseda);
    }
    return besede;
}

// ============================================================
// OSNOVNA LOGIKA WORDLE
// ============================================================

std::string get_coloring(const std::string& guess, const std::string& hidden) {
    auto g = utf8_v_unicode(guess);
    auto h = utf8_v_unicode(hidden);
    int n = g.size();

    std::string result(n, 'S');
    std::vector<bool> used_hidden(n, false);
    std::vector<bool> used_guess(n, false);

    for (int i = 0; i < n; i++) {
        if (g[i] == h[i]) {
            result[i] = 'Z';
            used_hidden[i] = true;
            used_guess[i] = true;
        }
    }
    for (int i = 0; i < n; i++) {
        if (used_guess[i]) continue;
        for (int j = 0; j < n; j++) {
            if (!used_hidden[j] && g[i] == h[j]) {
                result[i] = 'R';
                used_hidden[j] = true;
                break;
            }
        }
    }
    return result;
}

std::vector<std::string> filter_words(
    const std::vector<std::string>& kandidati,
    const std::string& guess,
    const std::string& coloring)
{
    std::vector<std::string> rezultat;
    for (const auto& beseda : kandidati)
        if (get_coloring(guess, beseda) == coloring)
            rezultat.push_back(beseda);
    return rezultat;
}

// ============================================================
// PREDIZRAČUN OBARVANJ (Optimizacija 3)
// ============================================================

void predizracunaj(
    const std::vector<std::string>& ugibanja,
    const std::vector<std::string>& kandidati)
{
    g_ugibanja.clear();
    g_kandidati.clear();
    g_ugibanja_idx.clear();
    g_kandidati_idx.clear();
    g_resitve_idx.clear();
    g_tabela.clear();

    g_ugibanja = ugibanja;
    g_kandidati = kandidati;

    for (int i = 0; i < (int)ugibanja.size(); i++)
        g_ugibanja_idx[ugibanja[i]] = i;
    for (int j = 0; j < (int)kandidati.size(); j++) {
        g_kandidati_idx[kandidati[j]] = j;
        g_resitve_idx[kandidati[j]] = j;
    }

    int st_ugibanj = ugibanja.size();
    int st_kandidati = kandidati.size();
    g_tabela.assign(st_ugibanj, std::vector<std::string>(st_kandidati));

    std::cout << "Predizracunavam " << st_ugibanj << "x" << st_kandidati << " obarvanj...\n";
    for (int i = 0; i < st_ugibanj; i++) {
        for (int j = 0; j < st_kandidati; j++)
            g_tabela[i][j] = get_coloring(ugibanja[i], kandidati[j]);
        if (i % 200 == 0)
            std::cout << "  " << i << "/" << st_ugibanj << "\n";
    }
    std::cout << "Predizracun koncan!\n\n";
}

// ============================================================
// PREHODI MED STANJI
// ============================================================

//slovar ključ[obarvanje] -> kandidati

std::unordered_map<std::string, std::vector<std::string>> get_transitions(
    const std::vector<std::string>& kandidati,
    const std::string& ugibanje)
{
    std::unordered_map<std::string, std::vector<std::string>> prehodi;
    auto ugibanje_it = g_ugibanja_idx.find(ugibanje);

    for (const auto& kandidat : kandidati) {
        std::string obarvanje;
        if (ugibanje_it != g_ugibanja_idx.end()) {
            auto kandidat_it = g_kandidati_idx.find(kandidat);
            if (kandidat_it != g_kandidati_idx.end())
                obarvanje = g_tabela[ugibanje_it->second][kandidat_it->second];
            else
                obarvanje = get_coloring(ugibanje, kandidat);
        }
        else {
            obarvanje = get_coloring(ugibanje, kandidat);
        }
        prehodi[obarvanje].push_back(kandidat);
    }
    return prehodi;
}

// ============================================================
// MEMOIZACIJA - BITSET KLJUČ
// ============================================================

std::string kljuc(const std::vector<std::string>& kandidati) {
    int n = (int)g_resitve_idx.size();
    std::string k((n / 8) + 1, '\0');
    for (const auto& b : kandidati) {
        auto it = g_resitve_idx.find(b);
        if (it != g_resitve_idx.end())
            k[it->second / 8] |= (char)(1 << (it->second % 8));
    }
    return k;
}

// ============================================================
// SPODNJA MEJA
// ============================================================

double spodnja_meja(int n) {
    return (2.0 * n - 1.0) / n;
}

// ============================================================
// DP ALGORITEM - BELLMANOVA ENAČBA
// ============================================================

double compute_V(
    const std::vector<std::string>& kandidati,
    const std::vector<std::string>& vsi_slovar,
    int t,
    int max_potez)
{
    int n = (int)kandidati.size();
#pragma omp atomic
    compute_calls++;

    if (compute_calls % 100000 == 0) {
        std::cout << "  calls=" << compute_calls
            << " t=" << t
            << " n=" << n
            << " memo=" << memo.size()
            << " hits=" << memo_hits << "\n";
        std::cout.flush();
    }

    int word_len = dolzina(kandidati[0]);
    std::string vse_zelene(word_len, 'Z');

    if (t >= max_potez || (t == max_potez - 1 && n > 1))
        return 1e18;

    if (n == 1) return 1.0;
    if (n == 2) return 1.5;

    // Preveri memoizacijsko tabelo – če je bil ta nabor kandidatov že izračunan, vrni shranjeno vrednost.
    std::string k = kljuc(kandidati);
    {
        std::lock_guard<std::mutex> lock(memo_mutex);
        auto it = memo.find(k);
        if (it != memo.end()) {
            memo_hits++;
            return it->second;
        }
    }

    // Optimizacija: kandidati se preizkusijo pred ostalimi besedami iz slovarja.
    double najboljsa_vrednost = 1e18;

    std::set<std::string> kandidati_set(kandidati.begin(), kandidati.end());
    std::vector<std::string> vrstni_red;
    for (const auto& b : kandidati)
        vrstni_red.push_back(b);
    for (const auto& b : vsi_slovar)
        if (!kandidati_set.count(b))
            vrstni_red.push_back(b);

#pragma omp parallel num_threads(8)
    {
        double lokalna_najboljsa = 1e18;

#pragma omp for schedule(dynamic, 1)
        for (int ui = 0; ui < (int)vrstni_red.size(); ui++) {
            const std::string& ugibanje = vrstni_red[ui];

            auto prehodi = get_transitions(kandidati, ugibanje);

            if (prehodi.size() == 1 &&
                prehodi.find(std::string(word_len, 'S')) != prehodi.end())
                continue;

            // izračunamo optimistično oceno vrednosti tega ugibanja
            double vrednost = 1.0;
            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                vrednost += (double)par.second.size() / n
                    * spodnja_meja((int)par.second.size());
            }

            if (vrednost >= lokalna_najboljsa)
                continue;

            std::vector<std::pair<double, std::vector<std::string>>> sortirani;
            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                sortirani.push_back(std::make_pair(
                    (double)par.second.size() / n, par.second));
            }
            std::sort(sortirani.begin(), sortirani.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });

            vrednost = 1.0;
            bool prune = false;

            for (const auto& par2 : sortirani) {
                double prob = par2.first;
                const std::vector<std::string>& naslednji = par2.second;

                double v_naslednji = compute_V(naslednji, vsi_slovar, t + 1, max_potez);
                vrednost += prob * v_naslednji;

                if (vrednost >= lokalna_najboljsa) {
                    prune = true;
                    break;
                }
            }

            if (!prune && vrednost < lokalna_najboljsa)
                lokalna_najboljsa = vrednost;
        }

#pragma omp critical
        {
            if (lokalna_najboljsa < najboljsa_vrednost)
                najboljsa_vrednost = lokalna_najboljsa;
        }
    }

    {
        std::lock_guard<std::mutex> lock(memo_mutex);
        memo[k] = najboljsa_vrednost;
    }

    return najboljsa_vrednost;
}

// ============================================================
// ENOSTAVNEJŠE STRATEGIJE
// ============================================================

std::string nakljucna_strategija(const std::vector<std::string>& kandidati) {
    return kandidati[rand() % kandidati.size()];
}

std::string hevristicna_strategija(const std::vector<std::string>& kandidati) {
    std::map<uint32_t, int> frekvenca;
    for (const auto& b : kandidati) {
        auto cp = utf8_v_unicode(b);
        for (auto c : cp) frekvenca[c]++;
    }
    std::string najboljsa;
    int max_score = -1;
    for (const auto& b : kandidati) {
        auto cp = utf8_v_unicode(b);
        std::set<uint32_t> unikatne(cp.begin(), cp.end());
        int score = 0;
        for (auto c : unikatne) score += frekvenca[c];
        if (score > max_score) { max_score = score; najboljsa = b; }
    }
    return najboljsa;
}

int simuliraj_igro(
    const std::string& skrita,
    const std::vector<std::string>& resitve,
    int strategija)
{
    std::vector<std::string> kandidati = resitve;
    int poteze = 0;
    int word_len = dolzina(skrita);
    std::string vse_zelene(word_len, 'Z');

    while (true) {
        poteze++;
        std::string ugibanje;
        if (strategija == 0) ugibanje = nakljucna_strategija(kandidati);
        else ugibanje = hevristicna_strategija(kandidati);

        std::string obarvanje = get_coloring(ugibanje, skrita);
        if (obarvanje == vse_zelene) return poteze;
        kandidati = filter_words(kandidati, ugibanje, obarvanje);
    }
}

double evaluiraj(
    const std::vector<std::string>& resitve,
    int strategija)
{
    double skupaj = 0;
    for (const auto& skrita : resitve)
        skupaj += simuliraj_igro(skrita, resitve, strategija);
    return skupaj / resitve.size();
}

// ============================================================
// EKSPERIMENTALNA POTRDITEV
// ============================================================

// Simulira eno igro z optimalno strategijo (bere iz memo)
int simuliraj_optimalno(
    const std::string& skrita,
    const std::vector<std::string>& resitve,
    int max_potez)
{
    std::vector<std::string> kandidati = resitve;
    int poteze = 0;
    int word_len = dolzina(skrita);
    std::string vse_zelene(word_len, 'Z');

    while (true) {
        poteze++;

        if (kandidati.size() == 1) {
            if (get_coloring(kandidati[0], skrita) == vse_zelene)
                return poteze;
        }

        // Poišči najboljše ugibanje med kandidati
        std::string najboljse = kandidati[0];
        double najboljsa_v = 1e18;

        for (const auto& ugibanje : kandidati) {
            auto prehodi = get_transitions(kandidati, ugibanje);
            double vrednost = 1.0;
            bool ok = true;

            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                double v = compute_V(par.second, resitve, 1, max_potez);
                if (v >= 1e17) { ok = false; break; }
                vrednost += (double)par.second.size() / kandidati.size() * v;
            }

            if (ok && vrednost < najboljsa_v) {
                najboljsa_v = vrednost;
                najboljse = ugibanje;
            }
        }

        std::string obarvanje = get_coloring(najboljse, skrita);
        if (obarvanje == vse_zelene) return poteze;
        kandidati = filter_words(kandidati, najboljse, obarvanje);
    }
}

// Evaluira optimalno strategijo in vrne povprečje
double evaluiraj_optimalno(
    const std::vector<std::string>& resitve,
    int max_potez)
{
    double skupaj = 0;
    std::map<int, int> porazdelitev;

    for (const auto& skrita : resitve) {
        int p = simuliraj_optimalno(skrita, resitve, max_potez);
        skupaj += p;
        porazdelitev[p]++;
    }

    double povprecje = skupaj / resitve.size();

    std::cout << "Porazdelitev ugibanj:\n";
    for (const auto& par : porazdelitev)
        std::cout << "  " << par.first << " ugibanj: " << par.second << " besed\n";

    return povprecje;
}

// ============================================================
// GLAVNA FUNKCIJA
// ============================================================

int main() {

    auto resitve_3_200 = nalozi_besede("resitve_3_200.txt");
    auto resitve_3_300 = nalozi_besede("resitve_3_300.txt");
    auto resitve_4_300 = nalozi_besede("resitve_4_300.txt");
    auto resitve_4_500 = nalozi_besede("resitve_4_500.txt");
    auto resitve_5_500 = nalozi_besede("resitve_5_500.txt");
    auto resitve_5_1861 = nalozi_besede("besedle_resitve.txt");
    auto resitve_6 = nalozi_besede("resitve_6.txt");
    auto resitve_7 = nalozi_besede("resitve_7.txt");
    auto resitve_8 = nalozi_besede("resitve_8.txt");
    auto resitve_9 = nalozi_besede("resitve_9.txt");
    auto resitve_10 = nalozi_besede("resitve_10.txt");
    auto resitve_11 = nalozi_besede("resitve_11.txt");
    auto resitve_12 = nalozi_besede("resitve_12.txt");
    auto resitve_13 = nalozi_besede("resitve_13.txt");
    auto resitve_14 = nalozi_besede("resitve_14.txt");
    auto resitve_15 = nalozi_besede("resitve_15.txt");

    struct Test {
        std::string ime;
        std::vector<std::string> besede;
        int max_potez;
    };

    std::vector<Test> testi = {
        {"3-crkovne (200, 9 potez)",          resitve_3_200,  9},
        {"3-crkovne (300, 9 potez)",          resitve_3_300,  9},
        {"4-crkovne (300, 8 potez)",          resitve_4_300,  8},
        {"4-crkovne (500, 8 potez)",          resitve_4_500,  8},
        {"5-crkovne (500, 6 potez)",          resitve_5_500,  6},
        {"5-crkovne Besedle (1861, 6 potez)", resitve_5_1861, 6},
        {"6-crkovne  (2566, 7 potez)",        resitve_6,      7},
        {"7-crkovne  (2943, 7 potez)",        resitve_7,      7},
        {"8-crkovne  (2926, 6 potez)",        resitve_8,      6},
        {"9-crkovne  (2684, 6 potez)",        resitve_9,      6},
        {"10-crkovne (2164, 5 potez)",        resitve_10,     5},
        {"11-crkovne (1613, 5 potez)",        resitve_11,     5},
        {"12-crkovne (1128, 4 potez)",        resitve_12,     4},
        {"13-crkovne (691,  4 potez)",        resitve_13,     4},
        {"14-crkovne (385,  3 potez)",        resitve_14,     3},
        {"15-crkovne (203,  3 potez)",        resitve_15,     3},
    };

    for (auto& test : testi) {
        predizracunaj(test.besede, test.besede);
        memo.clear();
        compute_calls = 0;
        memo_hits = 0;

        std::cout << "Racunam: " << test.ime << "...\n";
        std::cout.flush();

        // Korak 1: DP izračun
        auto start = std::chrono::high_resolution_clock::now();
        double v = compute_V(test.besede, test.besede, 0, test.max_potez);
        auto end = std::chrono::high_resolution_clock::now();
        double sek = std::chrono::duration<double>(end - start).count();

        //// Korak 2: eksperimentalna potrditev
        std::cout << "Eksperimentalna potrditev...\n";
        std::cout.flush();
        auto start_exp = std::chrono::high_resolution_clock::now();
        double exp_v = evaluiraj_optimalno(test.besede, test.max_potez);
        auto end_exp = std::chrono::high_resolution_clock::now();
        double sek_exp = std::chrono::duration<double>(end_exp - start_exp).count();

        //// Korak 3: primerjava strategij
        srand(42);
        double nakljucna = evaluiraj(test.besede, 0);
        double hevristicna = evaluiraj(test.besede, 1);

        // Korak 4: optimalni prvi ugib
        std::cout << "Iscemo optimalni prvi ugib...\n";
        std::cout.flush();
        std::string najboljsi_zacetek;
        double najboljsa_v_zacetek = 1e18;
        int word_len = dolzina(test.besede[0]);
        std::string vse_zelene(word_len, 'Z');

        for (const auto& ugibanje : test.besede) {
            auto prehodi = get_transitions(test.besede, ugibanje);
            double vrednost = 1.0;
            bool ok = true;
            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                double vn = compute_V(par.second, test.besede, 1, test.max_potez);
                if (vn >= 1e17) { ok = false; break; }
                vrednost += (double)par.second.size() / test.besede.size() * vn;
            }
            if (ok && vrednost < najboljsa_v_zacetek) {
                najboljsa_v_zacetek = vrednost;
                najboljsi_zacetek = ugibanje;
            }
        }

        std::cout << "\n=== " << test.ime << " ===\n";
        std::cout << "V* (izracunan):     " << v << "\n";
        std::cout << "V* (eksperiment):   " << exp_v << "\n";
        std::cout << "Razlika:            " << std::abs(v - exp_v) << "\n";
        std::cout << "Cas DP:             " << sek << "s\n";
        std::cout << "Cas eksperiment:    " << sek_exp << "s\n";
        std::cout << "Nakljucna:          " << nakljucna << "\n";
        std::cout << "Hevristicna:        " << hevristicna << "\n";
        std::cout << "Memo size:          " << memo.size() << "\n";
        std::cout << "Compute calls:      " << compute_calls << "\n";
        std::cout << "Optimalni zacetek:  " << najboljsi_zacetek << "\n\n";
        std::cout.flush();
    }

    return 0;
}