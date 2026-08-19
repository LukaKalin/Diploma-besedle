import os
import shutil
import random
from collections import Counter

# ============================================================
# NASTAVITVE
# ============================================================
INPUT_RESITVE = r"C:\Diploma\besede_resitve.txt"
OUTPUT_DIR = r"C:\Diploma\github\data"
RANDOM_SEED = 42

NABORI = {
    3:  200,
    4:  500,
    6:  None,
    7:  None,
    8:  None,
    9:  None,
    10: None,
    11: None,
    12: None,
    13: None,
    14: None,
    15: None,
}
# ============================================================

os.makedirs(OUTPUT_DIR, exist_ok=True)

with open(INPUT_RESITVE, encoding="utf-8") as f:
    vse_besede = f.read().splitlines()

random.seed(RANDOM_SEED)

for dolzina, stevilo in NABORI.items():
    besede = [b for b in vse_besede if len(b) == dolzina]
    if stevilo and len(besede) > stevilo:
        izbrane = sorted(random.sample(besede, stevilo))
    else:
        izbrane = sorted(besede)
    pot = os.path.join(OUTPUT_DIR, f"resitve_{dolzina}.txt")
    with open(pot, "w", encoding="utf-8") as f:
        for b in izbrane:
            f.write(b + "\n")
    print(f"{dolzina}-crkovnih: {len(izbrane)} besed → {pot}")

# Besedle datoteke
shutil.copy(r"C:\Diploma\besedle_resitve.txt",
            os.path.join(OUTPUT_DIR, "besedle_resitve.txt"))
shutil.copy(r"C:\Diploma\besedle_ugibanja.txt",
            os.path.join(OUTPUT_DIR, "besedle_ugibanja.txt"))

# 5-crkovne manjsi nabor iz Besedle
with open(r"C:\Diploma\besedle_resitve.txt", encoding="utf-8") as f:
    besedle = f.read().splitlines()

resitve_5_500 = sorted(random.sample(besedle, 500))
with open(os.path.join(OUTPUT_DIR, "resitve_5_500.txt"), "w", encoding="utf-8") as f:
    for b in resitve_5_500:
        f.write(b + "\n")
print(f"5-crkovnih (Besedle 500): {len(resitve_5_500)} besed")