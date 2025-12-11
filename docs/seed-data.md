# Seed Data Generation

Generate sample artwork images for demos and presentations.

## Related

- [Subfolder Artwork Generator](subfolder-artwork.md) — Generate placeholder artwork for subcollections

## Overview

The seed data generator creates randomly colored images with QR codes containing the filename. Each image has a unique, procedurally generated name using a combination of adjectives, nouns, prefixes, and suffixes.

**Combination capacity:** Over 11 billion unique filenames possible, easily supporting 1M+ items for stress testing.

## Setup

```bash
cd .scripts
python -m venv .venv
source .venv/bin/activate.fish  # or .venv/bin/activate for bash
pip install -r requirements.txt
```

## Usage

```bash
python generate_seed_data.py [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-n, --count` | Number of items to generate | 50 |
| `-o, --output-dir` | Output directory | `~/kartend-seed-data` |
| `-s, --subfolders` | Distribute files across random subfolders | disabled |
| `--subfolder-count` | Number of subfolders to create | random 5-15 |
| `--seed` | Random seed for reproducible generation | none |

### Examples

```bash
# Generate 50 images (default)
python generate_seed_data.py

# Generate 100 images to a custom directory
python generate_seed_data.py --count 100 --output-dir ~/kartend-demo

# Generate 500 images with subfolders
python generate_seed_data.py -n 500 -s

# Generate 200 images in exactly 10 subfolders
python generate_seed_data.py -n 200 --subfolders --subfolder-count 10

# Reproducible generation with seed
python generate_seed_data.py -n 100 --seed 42
```

## Output

The script creates PNG images in the output directory. Each image:

- Has a unique procedurally generated name (e.g., `Wobbly Cheese Wheel.png`)
- Contains a QR code encoding the filename
- Uses a randomly generated color scheme

After generation, the script outputs a sample configuration block for `kartend.cfg`:

```ini
[Demo Collection]
mediaDirectory=/path/to/output
artworkDirectory=/path/to/output
gridWidth=6
rowHeight=200
```

## Subfolder Mode

With `-s` or `--subfolders`, files are distributed randomly across subfolders with procedurally generated names (e.g., `Haunted Archives`, `Forbidden Vault`). Subfolder names are generated dynamically, so there is no limit to `--subfolder-count`. This is useful for testing the subfolder browsing feature.
