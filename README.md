# Optimalno reševanje igre Besedle

Diplomska naloga – Fakulteta za računalništvo in informatiko, Univerza v Ljubljani

**Avtor:** Luka Kalin  
**Mentor:** doc. dr. Tomaž Hočevar  
**Vir:** Bertsimas D, Paskov A. [An Exact and Interpretable Solution to Wordle](https://auction-upload-files.s3.amazonaws.com/Wordle_Paper_Final.pdf)

---

## Opis

Besedle je popularna slovenska različica besedne igre Wordle, kjer ugibamo skrito besedo s povratnimi informacijami o pravilnih črkah na pravih mestih in preostalih črkah, ki niso na pravem mestu.

Ta projekt implementira **optimalni algoritem za reševanje igre Besedle** z uporabo dinamičnega programiranja (Bellmanova enačba). Algoritem poišče strategijo ugibanja ki minimizira pričakovano število ugibanj.

---

## Struktura repozitorija

```
Diploma-besedle/
├── src/
│   └── wordle_solver.cpp    # Implementacija DP algoritma v C++
├── scripts/
│   ├── parse_sloleks.py     # Parser za morfološki leksikon Sloleks 3.0
│   └── prepare_wordlists.py # Priprava besednih list po dolžinah
├── data/
│   ├── besede_resitve.txt   # Vse rešitve iz Sloleks (frekvenca ≥ 200)
│   ├── besede_ugibanja.txt  # Vsa ugibanja iz Sloleks (frekvenca ≥ 1)
│   ├── besedle_resitve.txt  # Rešitve iz igre Besedle (1861 besed)
│   ├── besedle_ugibanja.txt # Ugibanja iz igre Besedle (3956 besed)
│   └── resitve_3.txt ...    # Besedne liste po dolžinah (3-15 črk)
└── results/
    └── ...                  # Rezultati izračunov
```

---

## Kako zagnati

### Zahteve
- C++ prevajalnik z podporo za C++17 in OpenMP
- Python 3.x (za skripte)

### Prevajanje (Linux)
```bash
g++ -O3 -fopenmp -o wordle_solver src/wordle_solver.cpp
```

### Prevajanje (Windows – Visual Studio)
Odpri projekt v Visual Studio 2022, nastavi:
- `C/C++` → `Language` → `OpenMP Support` → **Yes**
- Zgradi v **Release** načinu

### Zagon
```bash
./wordle_solver
```

### Priprava besednih list
```bash
# Najprej sparsaj Sloleks (zahteva Sloleks 3.0 XML datoteke)
python scripts/parse_sloleks.py

# Pripravi besedne liste po dolžinah
python scripts/prepare_wordlists.py
```

---

## Implementirane optimizacije

Iz članka Bertsimas & Paskov:
1. **Skrajšanje horizonta** – omejen čas ugibanj
2. **n=2 → V*=1.5** – robni pogoj brez rekurzije
3. **Predizračun obarvanj** – tabela vseh kombinacij
4. **Pruning s spodnjimi mejami** – zgodnje odrezanje slabih ugibanj
5. **Prioriteta kandidatom** – optimalen vrstni red ugibanj

Lastne optimizacije:
- **Paralelizacija z OpenMP** – vzporedno preizkušanje ugibanj
- **Thread-safe memoizacija** – mutex zaščita memo tabele
- **Bitset ključ** – kompaktna predstavitev množice kandidatov

---

## Rezultati

### Besedle (5-črkovne besede)
| Besed | Max potez | V*    | Optimalni začetek | Čas    |
|-------|-----------|-------|-------------------|--------|
| 500   | 6         | 3.002 | **TAROK**         | 21.5s  |
| 1861  | 6         | 3.395 | **TOREK**         | 129 ur |

### Sloleks baza – različne dolžine besed
| Dolžina | Besed | Max potez | V*    | Optimalni začetek    | Čas      |
|---------|-------|-----------|-------|----------------------|----------|
| 3       | 200   | 9         | 3.985 | sok                  | 39 min   |
| 3       | 300   | 9         | 4.347 | dok                  | 3.2 ure  |
| 4       | 300   | 8         | 3.317 | kalo                 | 16 min   |
| 4       | 500   | 8         | 3.558 | kalo                 | 5.1 ure  |
| 5       | 500   | 6         | 3.002 | tarok                | 21.5s    |
| 5       | 1861  | 6         | 3.395 | torek                | 129 ur   |
| 6       | 2566  | 7         | 3.080 | krtina               | 7.4 ure  |
| 7       | 2943  | 7         | 2.863 | karoten              | 1.5 ure  |
| 8       | 2926  | 6         | 2.692 | klarinet             | 2.1 min  |
| 9       | 2684  | 6         | 2.561 | podaritev            | 34.5s    |
| 10      | 2164  | 5         | 2.414 | polresnica           | 8.5s     |
| 11      | 1613  | 5         | 2.280 | redovalnica          | 2.3s     |
| 12      | 1128  | 4         | 2.176 | narkolepsija         | 0.76s    |
| 13      | 691   | 4         | 2.100 | razpredelnica        | 0.19s    |
| 14      | 385   | 3         | 2.034 | nesorazmernost       | 0.03s    |
| 15      | 203   | 3         | 2.005 | profesionalizem      | 0.006s   |

**Opažanja:**
- Z večjo dolžino besed V* pada – daljše besede so lažje ugibati ker vsako ugibanje da več informacije
- Z večjim naborom besed V* raste – večji slovar pomeni težjo igro
- Čas izračuna eksponentno raste z velikostjo nabora

---

## Eksperimentalna potrditev

Izračunani V* je bil preverjen z Monte Carlo simulacijo, ki igra Besedle z naključno izbranimi besedami in meri povprečno število ugibanj optimalne strategije. Simulacija ugiba le med preostalimi kandidati (ne more izkoristiti informacijskih ugibanj iz celotnega nabora besed), zato so eksperimentalni rezultati nekoliko višji od izračunanih.

| Dolžina | Besed | V* izračunan | V* eksperiment | Razlika |
|---------|-------|-------------|----------------|---------|
| 3       | 200   | 3.985       | 4.150          | 0.165   |
| 4       | 300   | 3.317       | 3.357          | 0.040   |
| 5       | 500   | 3.002       | 2.990          | 0.002   |
| 6       | 2566  | 3.080       | 3.137          | 0.057   |
| 7       | 2943  | 2.863       | 2.878          | 0.014   |
| 8       | 2926  | 2.692       | 2.707          | 0.015   |
| 9       | 2684  | 2.561       | 2.568          | 0.007   |
| 10      | 2164  | 2.414       | 2.418          | 0.004   |
| 11      | 1613  | 2.280       | 2.280          | ≈0      |
| 12      | 1128  | 2.176       | 2.176          | ≈0      |
| 13      | 691   | 2.100       | 2.100          | ≈0      |
| 14      | 385   | 2.034       | 2.034          | ≈0      |
| 15      | 203   | 2.005       | 2.005          | ≈0      |

Za besede dolžin 11–15 je razlika praktično nič (reda 10⁻¹⁴), kar je zgolj zaokrožitvena napaka.

---

## Primerjava strategij (5-črkovne Besedle besede)

| Strategija      | Povprečno število ugibanj |
|-----------------|---------------------------|
| Naključna       | ~4.02                     |
| Hevristična     | ~3.70                     |
| DP optimalna    | 3.395                     |

---

## Besedišče

Besede so pridobljene iz:
- **Sloleks 3.0** – morfološki leksikon slovenščine (CLARIN.SI)
- **Besedle** – slovenska različica igre Wordle ([besedle.com](https://besedle.com))
