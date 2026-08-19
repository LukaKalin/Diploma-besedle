import xml.etree.ElementTree as ET
import os
import glob
from collections import Counter

# ============================================================
# NASTAVITVE
# ============================================================
INPUT_DIR = r"C:\Diploma\Sloleks.3.0\Sloleks.3.0"
OUTPUT_RESITVE = r"C:\Diploma\besede_resitve.txt"
OUTPUT_UGIBANJA = r"C:\Diploma\besede_ugibanja.txt"

MIN_LENGTH = 3
MAX_LENGTH = 15

ALLOWED_CATEGORIES = {"noun", "adjective"}
ALLOWED_TYPE = "common"

FREQ_RESITVE = 200    # prag za rešitve
FREQ_UGIBANJA = 1     # prag za ugibanja (vse razen frekvenca 0)
# ============================================================

def parse_sloleks(input_dir):
    xml_files = glob.glob(os.path.join(input_dir, "*.xml"))
    print(f"Najdenih {len(xml_files)} XML datotek...")

    resitve = set()
    ugibanja = set()

    for i, xml_file in enumerate(xml_files):
        filename = os.path.basename(xml_file)
        print(f"[{i+1}/{len(xml_files)}] Obdelujem {filename}...")

        try:
            tree = ET.parse(xml_file)
            root = tree.getroot()

            for entry in root.findall(".//entry"):

                # 1. Kategorija
                category = entry.findtext(".//grammar/category", default="").strip()
                if category not in ALLOWED_CATEGORIES:
                    continue

                # 2. Tip (common / proper)
                word_type = ""
                for gf in entry.findall(".//grammar/grammarFeature"):
                    if gf.get("name") == "type":
                        word_type = gf.text.strip() if gf.text else ""
                        break
                if ALLOWED_TYPE and word_type != ALLOWED_TYPE:
                    continue

                # 3. Lema
                lemma = entry.findtext(".//headword/lemma", default="").strip().lower()
                if not lemma:
                    continue

                # 4. Dolžina
                if not (MIN_LENGTH <= len(lemma) <= MAX_LENGTH):
                    continue

                # 5. Samo abecedni znaki
                if not all(c.isalpha() for c in lemma):
                    continue

                # 6. Frekvenca
                freq = 0
                for measure in entry.findall(".//head/measureList/measure"):
                    if measure.get("type") == "frequency":
                        try:
                            freq = int(measure.text.strip())
                        except:
                            freq = 0
                        break

                # 7. Dodaj v ustrezni seznam
                if freq >= FREQ_UGIBANJA:
                    ugibanja.add(lemma)
                if freq >= FREQ_RESITVE:
                    resitve.add(lemma)

        except Exception as e:
            print(f"  Napaka pri {filename}: {e}")

    return sorted(resitve), sorted(ugibanja)

def shrani_in_statistika(besede, output_file, naziv):
    with open(output_file, "w", encoding="utf-8") as f:
        for beseda in besede:
            f.write(beseda + "\n")
    
    print(f"\n✓ {naziv}: {len(besede)} besed → {output_file}")
    print(f"  Statistika po dolžinah:")
    dolzine = Counter(len(b) for b in besede)
    for d in sorted(dolzine):
        print(f"    {d} črk: {dolzine[d]} besed")

if __name__ == "__main__":
    resitve, ugibanja = parse_sloleks(INPUT_DIR)
    shrani_in_statistika(resitve, OUTPUT_RESITVE, "REŠITVE")
    shrani_in_statistika(ugibanja, OUTPUT_UGIBANJA, "UGIBANJA")