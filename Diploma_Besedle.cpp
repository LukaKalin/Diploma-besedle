#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <windows.h>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <omp.h>
#include <mutex>

// ------------------------------------------------------------
// GLOBALNE SPREMENLJIVKE
// ------------------------------------------------------------

// Predizračunana tabela obarvanj [ugibanje_idx][kandidat_idx] → obarvanje
std::vector<std::string> g_ugibanja;
std::vector<std::string> g_kandidati;
std::map<std::string, int> g_ugibanja_idx;
std::map<std::string, int> g_kandidati_idx;
std::vector<std::vector<std::string>> g_tabela;

// Indeks rešitev za bitset ključ memoizacije
std::map<std::string, int> g_resitve_idx;

// Memoizacija - shranjuje že izračunane vrednosti V*(stanje)
std::unordered_map<std::string, double> memo;
std::mutex memo_mutex; // zaščita za sočasni dostop (OpenMP)
int memo_hits = 0;     // število zadetkov v memo tabeli
int compute_calls = 0; // skupno število klicev compute_V

// Časovniki za analizo učinkovitosti
double cas_transitions = 0; // čas izračuna prehodov
double cas_rekurzija = 0;   // čas rekurzivnih klicev
double cas_kljuc = 0;       // čas izračuna ključa

// ------------------------------------------------------------
// UTF-8 PODPORA
// ------------------------------------------------------------

// Pretvori UTF-8 niz v zaporedje Unicode kodnih točk
// Potrebno ker šumniki (š, č, ž) zavzamejo 2 bajta v UTF-8
std::vector<uint32_t> utf8_v_unicode(const std::string& s) {
    std::vector<uint32_t> result;
    int i = 0;
    while (i < (int)s.size()) {
        unsigned char c = s[i];
        uint32_t cp;
        if (c < 0x80) {
            // 1-bajtni znak (ASCII)
            cp = c; i++;
        }
        else if (c < 0xE0) {
            // 2-bajtni znak (šumniki)
            cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); i += 2;
        }
        else if (c < 0xF0) {
            // 3-bajtni znak (kitajski znaki...)
            cp = (c & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); i += 3;
        }
        else {
            // 4-bajtni znak (emoji...)
            cp = (c & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F); i += 4;
        }
        result.push_back(cp);
    }
    return result;
}

// Vrne dolžino besede v znakih (ne bajtih)
int dolzina(const std::string& s) {
    return (int)utf8_v_unicode(s).size();
}

// ------------------------------------------------------------
// BRANJE DATOTEK
// ------------------------------------------------------------

// Naloži seznam besed iz tekstovne datoteke (ena beseda na vrstico)
std::vector<std::string> nalozi_besede(const std::string& pot) {
    std::vector<std::string> besede;
    std::ifstream datoteka(pot);
    std::string beseda;
    while (std::getline(datoteka, beseda)) {
        // Odstrani \r pri Windows presledkih (\r\n)
        if (!beseda.empty() && beseda.back() == '\r')
            beseda.pop_back();
        if (!beseda.empty())
            besede.push_back(beseda);
    }
    return besede;
}

// ------------------------------------------------------------
// OSNOVNA LOGIKA WORDLE
// ------------------------------------------------------------

// Izračuna obarvanje ugibanja glede na skrito besedo:
//   Z (zelena)  = pravilna črka na pravem mestu
//   R (rumena)  = pravilna črka na napačnem mestu
//   S (siva)    = črka ni v besedi
std::string get_coloring(const std::string& guess, const std::string& hidden) {
    auto g = utf8_v_unicode(guess);
    auto h = utf8_v_unicode(hidden);
    int n = g.size();

    std::string result(n, 'S');
    std::vector<bool> used_hidden(n, false);
    std::vector<bool> used_guess(n, false);

    // Najprej poiščemo zelene (pravilna mesta)
    for (int i = 0; i < n; i++) {
        if (g[i] == h[i]) {
            result[i] = 'Z';
            used_hidden[i] = true;
            used_guess[i] = true;
        }
    }

    // Nato poiščemo rumene (napačna mesta)
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

// Filtrira seznam kandidatov – ohrani samo tiste besede,
// ki bi dale enako obarvanje kot ugibanje za skrito besedo
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

// ------------------------------------------------------------
// PREDIZRAČUN OBARVANJ (Optimizacija 3)
// ------------------------------------------------------------

// Vnaprej izračuna vse kombinacije obarvanj (ugibanje × kandidat)
// in jih shrani v tabelo za hitrejši dostop med algoritmom
void predizracunaj(
    const std::vector<std::string>& ugibanja,
    const std::vector<std::string>& kandidati)
{
    g_ugibanja = ugibanja;
    g_kandidati = kandidati;

    // Zgradi indekse za hitro iskanje
    for (int i = 0; i < (int)ugibanja.size(); i++)
        g_ugibanja_idx[ugibanja[i]] = i;
    for (int j = 0; j < (int)kandidati.size(); j++) {
        g_kandidati_idx[kandidati[j]] = j;
        g_resitve_idx[kandidati[j]] = j;
    }

    // Izračunaj tabelo obarvanj
    int nu = ugibanja.size();
    int nk = kandidati.size();
    g_tabela.assign(nu, std::vector<std::string>(nk));

    std::cout << "Predizracunavam " << nu << "x" << nk << " obarvanj...\n";
    for (int i = 0; i < nu; i++) {
        for (int j = 0; j < nk; j++)
            g_tabela[i][j] = get_coloring(ugibanja[i], kandidati[j]);
        //Izpis napredka pri računanju
        if (i % 200 == 0)
            std::cout << "  " << i << "/" << nu << "\n";
    }
    std::cout << "Predizracun koncan!\n\n";
}

// ------------------------------------------------------------
// PREHODI MED STANJI
// ------------------------------------------------------------

// Za dano ugibanje vrne razdelitev kandidatov po obarvanju:
//   obarvanje → seznam kandidatov ki ga producirajo
// Uporablja predizračunano tabelo za hitrost
std::unordered_map<std::string, std::vector<std::string>> get_transitions(
    const std::vector<std::string>& kandidati,
    const std::string& ugibanje)
{
    std::unordered_map<std::string, std::vector<std::string>> prehodi;
    // Find vrne par {ključ:indeks}
    auto u_it = g_ugibanja_idx.find(ugibanje);

    for (const auto& kandidat : kandidati) {
        std::string obarvanje;
        // Ekvivalentno: g_ugibanja_idx.contains(ugibanje)
        if (u_it != g_ugibanja_idx.end()) {
            // Hitro iskanje v predizračunani tabeli
            auto k_it = g_kandidati_idx.find(kandidat);
            if (k_it != g_kandidati_idx.end())
                obarvanje = g_tabela[u_it->second][k_it->second]; // u_it->second, je indeks
            else
                obarvanje = get_coloring(ugibanje, kandidat); // Edge case med testiranjem, normalno nepotrebno
        }
        else {
            obarvanje = get_coloring(ugibanje, kandidat);
        }
        prehodi[obarvanje].push_back(kandidat);
    }
    return prehodi;
}

// ------------------------------------------------------------
// MEMOIZACIJA - BITSET KLJUČ
// ------------------------------------------------------------

// Pretvori množico kandidatov v kompakten bitset ključ
// Vsaka beseda dobi en bit → enolično predstavlja množico
// Ključ je neodvisen od vrstnega reda kandidatov
// pospeši preverjanje
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

// ------------------------------------------------------------
// DP ALGORITEM - BELLMANOVA ENAČBA
// ------------------------------------------------------------

// Spodnja meja vrednosti za stanje velikosti n:
// (2n-1)/n = optimistična ocena brez globlje rekurzije
double spodnja_meja(int n) {
    return (2.0 * n - 1.0) / n;
}

// Rekurzivno izračuna optimalno pričakovano število ugibanj V*(stanje)
// za dano množico kandidatov pri potezi t
//
// Optimizacije iz članka:
//   Opt.1 - skrajšanje horizonta (max 6 potez)
//   Opt.2 - n=2 → V*=1.5 brez rekurzije
//   Opt.3 - predizračun obarvanj (v get_transitions)
//   Opt.4 - pruning s spodnjimi mejami
//   Opt.5 - prioriteta kandidatom pri vrstnem redu ugibanj
//
// Lastna optimizacija:
//   - paralelizacija zunanje zanke z OpenMP
//   - thread-safe memoizacija z mutex
double compute_V(
    const std::vector<std::string>& kandidati,
    const std::vector<std::string>& vsi_slovar,
    int t)
{
    int n = (int)kandidati.size();
    compute_calls++;

    int word_len = dolzina(kandidati[0]);
    std::string vse_zelene(word_len, 'Z');

    // Optimizacija 1: skrajšanje horizonta
    // Če na zadnji potezi ostane več kandidatov, torej zgubimo → neskončno
    if (t >= 6 || (t == 5 && n > 1))
        return 1e18;

    // Robni pogoj: en kandidat → takoj uganeš
    if (n == 1) return 1.0;

    // Optimizacija 2: dva kandidata → vedno 1.5
    // (50% takoj, 50% še ena poteza)
    if (n == 2) return 1.5;

    // Memoizacija: preveri ali smo to stanje že izračunali
    std::string k = kljuc(kandidati);
    {
        std::lock_guard<std::mutex> lock(memo_mutex);
        auto it = memo.find(k);
        if (it != memo.end()) {
            memo_hits++;
            return it->second;
        }
    }

    double najboljsa_vrednost = 1e18; // Neskončno → vsaka vrednost bo boljša od tega

    // Optimizacija 5: vrstni red ugibanj
    // Najprej preizkusi kandidate (pogosto optimalni),
    // potem ostale besede iz slovarja
    // Optimizacija 3: uporabi set za O(log n) iskanje
    std::set<std::string> kandidati_set(kandidati.begin(), kandidati.end());
    std::vector<std::string> vrstni_red;
    for (const auto& b : kandidati)
        vrstni_red.push_back(b);
    for (const auto& b : vsi_slovar)
        if (!kandidati_set.count(b))
            vrstni_red.push_back(b);

    // Lastna optimizacija: paralelna zanka čez ugibanja z OpenMP
#pragma omp parallel num_threads(8)
    {
        double lokalna_najboljsa = 1e18;

#pragma omp for schedule(dynamic, 1)
        for (int ui = 0; ui < (int)vrstni_red.size(); ui++) {
            const std::string& ugibanje = vrstni_red[ui];

            // Izračunaj prehode za to ugibanje
            auto t1 = std::chrono::high_resolution_clock::now();
            auto prehodi = get_transitions(kandidati, ugibanje);
            auto t2 = std::chrono::high_resolution_clock::now();
#pragma omp atomic
            cas_transitions += std::chrono::duration<double>(t2 - t1).count();

            // Preskoči ugibanje ki ne da nobene informacije
            if (prehodi.size() == 1 &&
                prehodi.find(std::string(word_len, 'S')) != prehodi.end())
                continue;

            // Optimizacija 4 (1. del): izračunaj spodnjo mejo vrednosti
            // z vsoto spodnjih mej za vsako naslednje stanje
            double vrednost = 1.0;
            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                vrednost += (double)par.second.size() / n
                    * spodnja_meja((int)par.second.size());
            }

            // Optimizacija 4: pruning
            // Če je že spodnja meja slabša od trenutnega minimuma, preskoči
            if (vrednost >= lokalna_najboljsa)
                continue;

            // Sortiraj naslednja stanja po verjetnosti (večja → najprej)
            // za zgodnejše pruning
            std::vector<std::pair<double, std::vector<std::string>>> sortirani;
            for (const auto& par : prehodi) {
                if (par.first == vse_zelene) continue;
                sortirani.push_back({ (double)par.second.size() / n, par.second });
            }
            std::sort(sortirani.begin(), sortirani.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });

            // Natančen rekurzivni izračun z sproti preverjenim pruningom
            vrednost = 1.0;
            bool prune = false;

            for (const auto& par2 : sortirani) {
                double prob = par2.first;
                const std::vector<std::string>& naslednji = par2.second;

                auto t3 = std::chrono::high_resolution_clock::now();
                double v_naslednji = compute_V(naslednji, vsi_slovar, t + 1);
                auto t4 = std::chrono::high_resolution_clock::now();
#pragma omp atomic
                cas_rekurzija += std::chrono::duration<double>(t4 - t3).count();

                vrednost += prob * v_naslednji;

                // Optimizacija 4: pruning med računanjem
                if (vrednost >= lokalna_najboljsa) {
                    prune = true;
                    break;
                }
            }

            if (!prune && vrednost < lokalna_najboljsa)
                lokalna_najboljsa = vrednost;
        }

        // Posodobi globalni minimum (thread-safe)
#pragma omp critical
        {
            if (lokalna_najboljsa < najboljsa_vrednost)
                najboljsa_vrednost = lokalna_najboljsa;
        }
    }

    // Shrani rezultat v memo tabelo (thread-safe)
    {
        std::lock_guard<std::mutex> lock(memo_mutex);
        memo[k] = najboljsa_vrednost;
    }

    return najboljsa_vrednost;
}

// ------------------------------------------------------------
// ENOSTAVNEJŠE STRATEGIJE (za primerjavo z DP)
// ------------------------------------------------------------

// Strategija 1: naključna izbira med preostalimi kandidati
std::string nakljucna_strategija(const std::vector<std::string>& kandidati) {
    return kandidati[rand() % kandidati.size()];
}

// Strategija 2: hevristična – ugibaj besedo z največ pogostimi črkami
std::string hevristicna_strategija(const std::vector<std::string>& kandidati) {
    // Preštej pogostost vsake črke med kandidati
    std::map<uint32_t, int> frekvenca;
    for (const auto& b : kandidati) {
        auto cp = utf8_v_unicode(b);
        for (auto c : cp) frekvenca[c]++;
    }
    // Izberi besedo z najvišjim seštevkom frekvenc unikatnih črk
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

// Simulira eno igro za dano strategijo – vrne število ugibanj
int simuliraj_igro(
    const std::string& skrita,
    const std::vector<std::string>& resitve,
    int strategija) // 0 = naključna, 1 = hevristična
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

// Evaluira povprečno število ugibanj strategije čez vse rešitve
double evaluiraj(
    const std::vector<std::string>& resitve,
    int strategija)
{
    double skupaj = 0;
    for (const auto& skrita : resitve)
        skupaj += simuliraj_igro(skrita, resitve, strategija);
    return skupaj / resitve.size();
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------

int main() {
    SetConsoleOutputCP(65001); // UTF-8 izpis v Windows konzoli

    // Naloži slovarje
    auto resitve = nalozi_besede("C:\\Diploma\\besedle_resitve.txt");
    auto ugibanja = nalozi_besede("C:\\Diploma\\besedle_ugibanja.txt");

    std::cout << "Resitve: " << resitve.size() << "\n";
    std::cout << "Ugibanja: " << ugibanja.size() << "\n\n";

    // Predizračun tabele obarvanj
    auto t_start = std::chrono::high_resolution_clock::now();
    predizracunaj(ugibanja, resitve);
    auto t_pred = std::chrono::high_resolution_clock::now();
    std::cout << "Predizracun: "
        << std::chrono::duration<double>(t_pred - t_start).count()
        << "s\n\n";

    // Resetiraj statistike
    memo.clear();
    compute_calls = 0;
    memo_hits = 0;
    cas_transitions = cas_rekurzija = cas_kljuc = 0;

    std::vector<std::string> test = resitve; // vse besede

    // Iskanje optimalne začetne besede:
    // Za vsako možno začetno besedo izračunamo vrednost V*
    // in obdržimo tisto z najnižjo vrednostjo
    std::string najboljsa_zacetna = "";
    double najboljsa_v = 1e17;

    std::cout << "Racunam optimalno zacetno besedo...\n\n";
    auto start = std::chrono::high_resolution_clock::now();

    for (int zi = 0; zi < (int)test.size(); zi++) {
        const std::string& zacetna = test[zi];

        // Izračunaj prehode za to začetno besedo
        auto prehodi = get_transitions(test, zacetna);

        // Seštej vrednosti vseh naslednih stanj
        double vrednost = 1.0;
        bool ok = true;

        for (const auto& par : prehodi) {
            if (par.first == std::string(5, 'Z')) continue;
            double v_sub = compute_V(par.second, test, 1);
            if (v_sub >= 1e17) { ok = false; break; }
            vrednost += (double)par.second.size() / test.size() * v_sub;
        }

        // Posodobi najboljšo začetno besedo
        if (ok && vrednost < najboljsa_v) {
            najboljsa_v = vrednost;
            najboljsa_zacetna = zacetna;
            auto now = std::chrono::high_resolution_clock::now();
            double sek = std::chrono::duration<double>(now - start).count();
            std::cout << "[" << zi + 1 << "/" << test.size() << "] "
                << "Nova najboljsa: " << zacetna
                << " V*=" << vrednost
                << " cas=" << sek << "s"
                << " memo=" << memo.size() << "\n";
        }
        else if ((zi + 1) % 10 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double sek = std::chrono::duration<double>(now - start).count();
            std::cout << "[" << zi + 1 << "/" << test.size() << "] "
                << "cas=" << sek << "s"
                << " memo=" << memo.size()
                << " hits=" << memo_hits << "\n";
        }
    }

    // Izpis končnega rezultata
    auto end = std::chrono::high_resolution_clock::now();
    double skupaj = std::chrono::duration<double>(end - start).count();

    std::cout << "\n=== REZULTAT ===\n";
    std::cout << "Najboljsa zacetna beseda: " << najboljsa_zacetna << "\n";
    std::cout << "V* = " << najboljsa_v << "\n";
    std::cout << "Skupaj cas: " << skupaj << "s\n";
    std::cout << "Memo size: " << memo.size() << "\n";
    std::cout << "Memo hits: " << memo_hits << "\n";
    std::cout << "Compute calls: " << compute_calls << "\n";

    return 0;
}