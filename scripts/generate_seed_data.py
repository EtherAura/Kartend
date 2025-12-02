#!/usr/bin/env python3
"""
Kartend Seed Data Generator

Generates sample artwork images for presentations/demos.
Creates randomly colored images with QR codes containing the filename.

Combination Capacity:
    - ADJECTIVES: ~260 items
    - NOUNS: ~280 items  
    - SUFFIXES: ~120 items
    - PREFIXES: ~45 items
    - NUMBERS: ~30 items
    
    Basic combos: 260 × 280 × 120 = ~8.7 million
    With prefixes: 45 × 260 × 280 × 120 = ~392 million
    With numbers: 260 × 280 × 30 × 120 = ~262 million
    Full combos: 45 × 260 × 280 × 30 × 120 = ~11.8 billion
    
    Easily supports 1 million+ unique filenames.

Usage:
    # Setup venv first:
    cd scripts
    python -m venv venv
    source venv/bin/activate.fish  # or activate.sh for bash
    pip install -r requirements.txt
    
    # Then run:
    python generate_seed_data.py [--count N] [--output-dir DIR]
    
Examples:
    python generate_seed_data.py --count 50
    python generate_seed_data.py --count 100 --output-dir ~/kartend-demo
"""

import argparse
import hashlib
import os
import random
import colorsys
from pathlib import Path

try:
    import qrcode
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Required dependencies not found. Setup a venv first:")
    print("  cd scripts")
    print("  python -m venv venv")
    print("  source venv/bin/activate.fish  # or activate for bash")
    print("  pip install -r requirements.txt")
    exit(1)


# Silly name components for generating ridiculous filenames
ADJECTIVES = [
    # Original
    "Wobbly", "Suspicious", "Caffeinated", "Slightly Damp", "Overconfident",
    "Bewildered", "Chaotic", "Fancy", "Grumpy", "Mysterious", "Sparkly",
    "Questionable", "Aggressively Beige", "Mildly Spicy", "Sentient",
    "Overly Enthusiastic", "Dramatically Lit", "Discount", "Premium",
    "Artisanal", "Organic", "Gluten-Free", "Existential", "Bureaucratic",
    "Passive-Aggressive", "Haunted", "Bootleg", "Suspiciously Cheap",
    "Forbidden", "Cursed", "Blessed", "Unhinged", "Feral", "Domesticated",
    # Emotions & States
    "Anxious", "Melancholic", "Ecstatic", "Apathetic", "Paranoid",
    "Nostalgic", "Euphoric", "Disgruntled", "Wistful", "Exasperated",
    "Befuddled", "Flabbergasted", "Indignant", "Perplexed", "Vexed",
    "Bemused", "Discombobulated", "Nonplussed", "Underwhelmed", "Overwhelmed",
    # Physical Properties
    "Crispy", "Soggy", "Crunchy", "Mushy", "Gelatinous", "Viscous",
    "Translucent", "Opaque", "Luminescent", "Iridescent", "Matte",
    "Glossy", "Fuzzy", "Prickly", "Squishy", "Rubbery", "Metallic",
    "Wooden", "Crystalline", "Powdery", "Gooey", "Flaky", "Crusty",
    # Size & Shape
    "Miniature", "Gigantic", "Microscopic", "Colossal", "Compact",
    "Elongated", "Spherical", "Cuboid", "Amorphous", "Asymmetrical",
    "Lopsided", "Perfectly Square", "Oddly Shaped", "Misshapen",
    # Temperature & Weather
    "Lukewarm", "Tepid", "Scorching", "Frigid", "Balmy", "Humid",
    "Arid", "Frosty", "Steamy", "Breezy", "Stormy", "Overcast",
    # Time-Related
    "Vintage", "Antique", "Futuristic", "Retro", "Prehistoric",
    "Medieval", "Victorian", "Post-Apocalyptic", "Timeless", "Expired",
    "Fresh", "Stale", "Aged", "Premature", "Overdue", "Belated",
    # Intensity
    "Extremely", "Mildly", "Barely", "Violently", "Gently", "Aggressively",
    "Subtly", "Dramatically", "Ridiculously", "Absurdly", "Moderately",
    "Excessively", "Insufficiently", "Overwhelmingly", "Underwhelming",
    # Tech & Modern
    "Wireless", "Bluetooth", "Solar-Powered", "Battery-Operated",
    "Voice-Activated", "AI-Enhanced", "Cloud-Based", "Encrypted",
    "Open-Source", "Proprietary", "Beta", "Legacy", "Deprecated",
    "Experimental", "Quantum", "Analog", "Digital", "Holographic",
    # Food-Adjacent
    "Marinated", "Fermented", "Pickled", "Smoked", "Caramelized",
    "Sautéed", "Deep-Fried", "Raw", "Undercooked", "Burnt", "Seasoned",
    "Unseasoned", "Sugar-Free", "Low-Sodium", "Extra Crispy",
    # Personality
    "Introverted", "Extroverted", "Ambitious", "Laid-Back", "Neurotic",
    "Stoic", "Dramatic", "Sarcastic", "Sincere", "Pretentious",
    "Humble", "Arrogant", "Timid", "Bold", "Reckless", "Cautious",
    # Colors as Adjectives
    "Neon", "Pastel", "Muted", "Vibrant", "Monochrome", "Technicolor",
    "Sepia-Toned", "Grayscale", "Rainbow", "Camouflage", "Tie-Dye",
    # Sound-Related
    "Squeaky", "Silent", "Thunderous", "Whispering", "Buzzing",
    "Humming", "Crackling", "Rumbling", "Echoing", "Muffled",
    # Smell-Related
    "Fragrant", "Pungent", "Musty", "Fresh-Scented", "Odorless",
    "Pine-Scented", "Lavender-Infused", "Suspicious-Smelling",
    # Texture
    "Velvety", "Silky", "Coarse", "Smooth", "Rough", "Bumpy",
    "Ridged", "Grooved", "Polished", "Weathered", "Corroded",
    # Status
    "Certified", "Uncertified", "Licensed", "Unlicensed", "Registered",
    "Unregistered", "Official", "Unofficial", "Authorized", "Rogue",
    "Sanctioned", "Unsanctioned", "Approved", "Rejected", "Pending",
    # Miscellaneous
    "Spontaneous", "Calculated", "Accidental", "Intentional", "Random",
    "Systematic", "Chaotic", "Orderly", "Improvised", "Rehearsed",
    "Genuine", "Counterfeit", "Authentic", "Replica", "Original",
    "Derivative", "Innovative", "Traditional", "Unconventional",
    "Mainstream", "Underground", "Viral", "Obscure", "Legendary",
    "Mythical", "Mundane", "Extraordinary", "Mediocre", "Exceptional",
    "Average", "Subpar", "Superior", "Inferior", "Adequate",
    "Insufficient", "Redundant", "Essential", "Optional", "Mandatory",
    "Voluntary", "Compulsory", "Flexible", "Rigid", "Adaptable",
    "Stubborn", "Cooperative", "Defiant", "Compliant", "Rebellious",
    "Obedient", "Mischievous", "Well-Behaved", "Rowdy", "Composed",
    "Frantic", "Serene", "Turbulent", "Peaceful", "Hostile",
    "Friendly", "Indifferent", "Passionate", "Dispassionate",
    "Enthusiastic", "Reluctant", "Eager", "Hesitant", "Confident",
    "Doubtful", "Certain", "Uncertain", "Ambiguous", "Clear",
    "Confusing", "Straightforward", "Complicated", "Simple", "Complex",
    "Elaborate", "Minimalist", "Maximalist", "Balanced", "Skewed",
    "Proportional", "Disproportionate", "Symmetrical", "Irregular",
    "Predictable", "Unpredictable", "Reliable", "Unreliable", "Stable",
    "Unstable", "Volatile", "Inert", "Reactive", "Passive", "Active",
    "Dormant", "Awakened", "Slumbering", "Alert", "Drowsy", "Energetic",
    "Lethargic", "Hyperactive", "Comatose", "Conscious", "Unconscious",
]

NOUNS = [
    # Original
    "Cheese Wheel", "Spreadsheet", "Tax Return", "Office Chair", "Stapler",
    "Sock Drawer", "Bread Loaf", "Traffic Cone", "Parking Meter", "Lint Trap",
    "Salad Fork", "Door Knob", "Ceiling Fan", "Rubber Duck", "Potato Salad",
    "Filing Cabinet", "Cardboard Box", "Lamp Shade", "Toast Crumb", "Dust Bunny",
    "Tupperware Lid", "Missing Remote", "Junk Drawer", "Expired Coupon",
    "Participation Trophy", "Forgotten Password", "Terms of Service",
    "Cookie Policy", "User Agreement", "Loading Screen", "Error Message",
    "Captcha", "Pop-up Ad", "Newsletter", "Unread Email", "Meeting Invite",
    # Office & Work
    "Paperclip", "Sticky Note", "Whiteboard Marker", "Swivel Chair",
    "Water Cooler", "Coffee Mug", "Desk Plant", "Monitor Stand", "USB Cable",
    "Keyboard", "Mouse Pad", "Desk Lamp", "Paper Shredder", "Hole Puncher",
    "Binder Clip", "Highlighter", "Index Card", "Manila Folder", "Inbox",
    "Outbox", "Voicemail", "Conference Call", "Zoom Background", "Slack Message",
    "Spreadsheet Formula", "Pivot Table", "Bar Graph", "Pie Chart", "Flowchart",
    # Kitchen & Food
    "Spatula", "Colander", "Cheese Grater", "Can Opener", "Bottle Cap",
    "Ice Cube Tray", "Oven Mitt", "Cutting Board", "Rolling Pin", "Whisk",
    "Measuring Cup", "Toaster", "Blender", "Microwave", "Refrigerator Magnet",
    "Grocery List", "Recipe Card", "Takeout Menu", "Fortune Cookie", "Tea Bag",
    "Coffee Filter", "Pizza Cutter", "Garlic Press", "Egg Timer", "Spice Rack",
    "Bread Crumb", "Pickle Jar", "Leftover Container", "Freezer Burn", "Expiration Date",
    # Household
    "Doormat", "Coat Hanger", "Shower Curtain", "Bath Towel", "Toilet Paper Roll",
    "Light Switch", "Power Outlet", "Extension Cord", "Vacuum Cleaner", "Mop Bucket",
    "Laundry Basket", "Ironing Board", "Smoke Detector", "Thermostat", "Air Filter",
    "Window Blinds", "Curtain Rod", "Picture Frame", "Bookshelf", "Couch Cushion",
    "Throw Pillow", "Area Rug", "Welcome Mat", "House Key", "Mailbox",
    "Garden Hose", "Lawn Gnome", "Bird Feeder", "Wind Chime", "Porch Light",
    # Technology
    "Ethernet Cable", "Router", "Modem", "Hard Drive", "RAM Stick",
    "Graphics Card", "Power Supply", "Cooling Fan", "Heatsink", "Motherboard",
    "Processor", "Solid State Drive", "Optical Drive", "Floppy Disk", "Zip Drive",
    "Flash Drive", "Memory Card", "SIM Card", "Battery Pack", "Charger",
    "Adapter", "Dongle", "Webcam", "Microphone", "Speaker",
    "Headphone Jack", "HDMI Port", "USB Hub", "Docking Station", "Laptop Stand",
    "Screen Protector", "Phone Case", "Stylus", "Trackpad", "Touch Screen",
    "Notification", "System Update", "Bug Report", "Crash Log", "Stack Trace",
    # Transportation
    "Bicycle Wheel", "Tire Pressure", "Windshield Wiper", "Turn Signal",
    "Rearview Mirror", "Fuel Tank", "Engine Block", "Spark Plug", "Oil Filter",
    "Brake Pad", "Steering Wheel", "Gear Shift", "Clutch Pedal", "Dashboard",
    "Glove Compartment", "Trunk Space", "Roof Rack", "Trailer Hitch", "License Plate",
    "Parking Ticket", "Toll Booth", "Speed Bump", "Crosswalk", "Bus Stop",
    # Nature
    "Pine Cone", "Acorn", "Maple Leaf", "Dandelion", "Four-Leaf Clover",
    "Mushroom Cap", "Tree Bark", "River Stone", "Sand Grain", "Snowflake",
    "Rain Drop", "Mud Puddle", "Tumbleweed", "Seashell", "Coral Reef",
    "Tide Pool", "Mountain Peak", "Valley Floor", "Canyon Wall", "Desert Dune",
    # Abstract & Concepts
    "Deadline", "Procrastination", "Motivation", "Inspiration", "Perspiration",
    "Obligation", "Recommendation", "Expectation", "Frustration", "Celebration",
    "Explanation", "Complication", "Simplification", "Notification", "Verification",
    "Authorization", "Calculation", "Estimation", "Speculation", "Contemplation",
    "Meditation", "Hesitation", "Determination", "Imagination", "Realization",
    # Events & Activities
    "Birthday Party", "Staff Meeting", "Dental Appointment", "Oil Change",
    "Grocery Run", "Laundry Day", "Spring Cleaning", "Tax Season", "Moving Day",
    "First Date", "Job Interview", "Performance Review", "Exit Interview",
    "Retirement Party", "Baby Shower", "Wedding Reception", "Funeral Procession",
    "Graduation Ceremony", "Award Show", "Press Conference", "Town Hall",
    # Documents & Media
    "Terms and Conditions", "Privacy Policy", "End User License", "Warranty Card",
    "Receipt", "Invoice", "Purchase Order", "Shipping Label", "Return Form",
    "Complaint Letter", "Thank You Note", "Birthday Card", "Post-It Note",
    "Memo", "Report Card", "Permission Slip", "Hall Pass", "Library Card",
    "Membership Badge", "Visitor Pass", "Parking Permit", "Concert Ticket",
    "Movie Stub", "Boarding Pass", "Baggage Claim", "Lost and Found",
    # Clothing & Accessories
    "Shoelace", "Belt Buckle", "Zipper", "Button", "Pocket Lint",
    "Collar", "Cuff Link", "Tie Clip", "Lapel Pin", "Name Tag",
    "Lanyard", "Wristband", "Watch Battery", "Earring Back", "Nose Ring",
    # Sports & Games
    "Dice Roll", "Card Deck", "Chess Piece", "Monopoly Money", "Game Board",
    "Score Card", "Referee Whistle", "Penalty Flag", "Goal Post", "Finish Line",
    "Starting Block", "Hurdle", "Javelin", "Discus", "Shot Put",
    "Bowling Pin", "Pool Cue", "Dart Board", "Ping Pong Ball", "Tennis Racket",
    # Medical & Health
    "Band-Aid", "Cotton Ball", "Q-Tip", "Thermometer", "Blood Pressure Cuff",
    "Prescription Bottle", "Pill Organizer", "Eye Drops", "Nasal Spray",
    "Cough Syrup", "Vitamin Tablet", "Heating Pad", "Ice Pack", "Splint",
    "Wheelchair", "Crutch", "Walking Cane", "Hospital Gown", "Surgical Mask",
    # Education
    "Textbook", "Notebook", "Pencil Sharpener", "Eraser", "Protractor",
    "Compass", "Calculator", "Globe", "Periodic Table", "Chalkboard",
    "Detention Slip", "Report Card", "Diploma", "Degree", "Transcript",
    "Syllabus", "Pop Quiz", "Final Exam", "Group Project", "Extra Credit",
    # Miscellaneous Objects
    "Toothpick", "Paperweight", "Snow Globe", "Lava Lamp", "Stress Ball",
    "Fidget Spinner", "Rubik's Cube", "Magic 8-Ball", "Crystal Ball", "Mood Ring",
    "Friendship Bracelet", "Keychain", "Bottle Opener", "Corkscrew", "Swiss Army Knife",
    "Flashlight", "Magnifying Glass", "Binoculars", "Telescope", "Microscope",
    "Compass", "Sundial", "Hourglass", "Metronome", "Tuning Fork",
    "Kazoo", "Harmonica", "Triangle", "Tambourine", "Cowbell",
    "Bubble Wrap", "Packing Peanut", "Shipping Tape", "Box Cutter", "Label Maker",
    "Stamp Collection", "Coin Collection", "Trading Card", "Action Figure", "Bobblehead",
    "Fridge Magnet", "Souvenir Spoon", "Snow Globe", "Postcard", "Bumper Sticker",
]

SUFFIXES = [
    # Original
    "Deluxe", "Turbo", "Pro Max", "Lite", "Premium", "Basic", "Ultimate",
    "Remastered", "Director's Cut", "Extended Edition", "The Sequel",
    "Returns", "Strikes Back", "Revenge", "Redemption", "Origins",
    "2: Electric Boogaloo", "3D", "HD", "4K", "Season Pass", "DLC",
    "Game of the Year", "Definitive Edition", "Anniversary", "",
    # Version Numbers
    "2.0", "3.0", "4.0", "5.0", "X", "XL", "XXL", "Mini", "Micro", "Nano",
    "Plus", "Plus Plus", "Max", "Ultra", "Super", "Mega", "Hyper", "Extreme",
    # Sequel Naming
    "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
    "Part 2", "Part 3", "Part 4", "Volume 2", "Volume 3", "Chapter 2",
    "Episode 2", "Season 2", "Season 3", "The Next Generation", "Legacy",
    "Generations", "Evolution", "Revolution", "Resurrection", "Rebirth",
    "Reborn", "Renewed", "Revamped", "Revisited", "Reimagined", "Rebooted",
    # Special Editions
    "Collector's Edition", "Limited Edition", "Special Edition", "Gold Edition",
    "Silver Edition", "Platinum Edition", "Diamond Edition", "Sapphire Edition",
    "Ruby Edition", "Emerald Edition", "Crystal Edition", "Titanium Edition",
    "Steelbook Edition", "Day One Edition", "Launch Edition", "First Edition",
    "Final Edition", "Complete Edition", "Enhanced Edition", "Expanded Edition",
    # Quality Tiers
    "Standard", "Advanced", "Professional", "Enterprise", "Personal",
    "Home", "Business", "Corporate", "Academic", "Student", "Teacher",
    "Starter", "Essential", "Classic", "Modern", "Contemporary",
    # Status
    "Beta", "Alpha", "Gamma", "Delta", "Release Candidate", "Stable",
    "Unstable", "Experimental", "Preview", "Early Access", "Public Test",
    "Insider", "Developer", "Debug", "Production", "Staging",
    # Regional
    "International", "Global", "Worldwide", "Regional", "Local",
    "North American", "European", "Asian", "Pacific", "Atlantic",
    # Time-Based
    "2024", "2025", "2026", "Forever", "Eternal", "Infinite", "Timeless",
    "Vintage", "Retro", "Classic", "Modern", "Future", "Next-Gen",
    # Descriptive
    "Uncut", "Uncensored", "Unrated", "Approved", "Certified", "Verified",
    "Authentic", "Original", "Genuine", "Official", "Licensed", "Authorized",
    # Humorous
    "Now With More Cowbell", "Banana for Scale", "Send Help", "Why Though",
    "Please Clap", "No Refunds", "As Seen on TV", "Not a Drill",
    "This Is Fine", "Much Wow", "Very Nice", "Great Success",
    "The Musical", "On Ice", "In Space", "Underwater", "Underground",
    "After Dark", "Unplugged", "Acoustic", "Live", "Studio",
    "Remix", "Mashup", "Cover", "Tribute", "Parody", "Bootleg",
    "Fan Edition", "Community Edition", "Open Source", "Freeware", "Shareware",
    "Trial Version", "Demo", "Sample", "Prototype", "Concept",
    # Empty string for no suffix
    "", "", "", "", "",
]

# Additional prefix words that can be combined
PREFIXES = [
    "The", "A", "An", "My", "Your", "Our", "Their", "This", "That",
    "Some", "Any", "Every", "No", "Another", "Yet Another",
    "Super", "Ultra", "Mega", "Hyper", "Meta", "Proto", "Neo", "Retro",
    "Pseudo", "Quasi", "Semi", "Anti", "Pro", "Pre", "Post", "Mid",
    "Dr.", "Professor", "Captain", "Admiral", "General", "Agent",
    "Sir", "Lord", "Lady", "Duke", "Duchess", "Count", "Baron",
]

# Numbers and years that can be appended
NUMBERS = [
    "1", "2", "3", "4", "5", "7", "9", "10", "13", "42", "69", "99", "100",
    "101", "200", "300", "404", "500", "666", "777", "888", "999", "1000",
    "2000", "3000", "9000", "9001", "2024", "2025", "2077", "3000",
]

# Action verbs for dynamic names
VERBS = [
    "Attacks", "Invades", "Conquers", "Discovers", "Explores", "Escapes",
    "Returns", "Awakens", "Strikes", "Rises", "Falls", "Crashes", "Explodes",
    "Implodes", "Transforms", "Evolves", "Mutates", "Assimilates", "Dominates",
    "Liberates", "Decimates", "Annihilates", "Obliterates", "Eradicates",
    "Investigates", "Interrogates", "Negotiates", "Meditates", "Procrastinates",
    "Hibernates", "Percolates", "Oscillates", "Fluctuates", "Accumulates",
    "Meets", "Fights", "Loves", "Hates", "Ignores", "Befriends", "Betrays",
    "Saves", "Destroys", "Creates", "Builds", "Breaks", "Fixes", "Ruins",
    "Finds", "Loses", "Steals", "Gives", "Takes", "Shares", "Hoards",
    "Celebrates", "Mourns", "Panics", "Relaxes", "Overthinks", "Underestimates",
]

# Connectors for compound names
CONNECTORS = [
    "vs", "and", "or", "meets", "versus", "&", "with", "without",
    "against", "plus", "featuring", "ft.", "x", "×",
]


def generate_filename(index: int, used_names: set) -> str:
    """Generate a unique silly filename.
    
    Uses multiple strategies to ensure uniqueness across millions of files:
    1. Basic: Adjective + Noun + Suffix (~8.7M combinations)
    2. With Prefix: Prefix + Adjective + Noun + Suffix (~392M combinations)
    3. With Number: Adjective + Noun + Number + Suffix (~262M combinations)
    4. With Verb: Noun + Verb + Suffix (action-style names)
    5. Compound: Noun + Connector + Noun + Suffix (crossover names)
    6. Full: Prefix + Adjective + Noun + Number + Suffix (~11.8B combinations)
    7. Fallback: Indexed name with hash (guaranteed unique)
    """
    max_attempts = 300
    
    for attempt in range(max_attempts):
        adjective = random.choice(ADJECTIVES)
        noun = random.choice(NOUNS)
        suffix = random.choice(SUFFIXES)
        
        # Vary the structure based on attempt to maximize uniqueness
        if attempt < 40:
            # Basic structure: "Wobbly Cheese Wheel Deluxe"
            if suffix:
                name = f"{adjective} {noun} {suffix}"
            else:
                name = f"{adjective} {noun}"
        elif attempt < 80:
            # Add prefix: "The Wobbly Cheese Wheel"
            prefix = random.choice(PREFIXES)
            if suffix:
                name = f"{prefix} {adjective} {noun} {suffix}"
            else:
                name = f"{prefix} {adjective} {noun}"
        elif attempt < 120:
            # Add number: "Wobbly Cheese Wheel 2000"
            number = random.choice(NUMBERS)
            if suffix:
                name = f"{adjective} {noun} {number} {suffix}"
            else:
                name = f"{adjective} {noun} {number}"
        elif attempt < 160:
            # Verb style: "Cheese Wheel Attacks" or "The Cheese Wheel Awakens"
            verb = random.choice(VERBS)
            prefix = random.choice(PREFIXES) if random.random() > 0.5 else ""
            if prefix:
                if suffix:
                    name = f"{prefix} {noun} {verb} {suffix}"
                else:
                    name = f"{prefix} {noun} {verb}"
            else:
                if suffix:
                    name = f"{noun} {verb} {suffix}"
                else:
                    name = f"{noun} {verb}"
        elif attempt < 200:
            # Compound crossover: "Cheese Wheel vs Traffic Cone"
            noun2 = random.choice(NOUNS)
            connector = random.choice(CONNECTORS)
            if suffix:
                name = f"{noun} {connector} {noun2} {suffix}"
            else:
                name = f"{noun} {connector} {noun2}"
        elif attempt < 250:
            # Full combination with prefix and number
            prefix = random.choice(PREFIXES)
            number = random.choice(NUMBERS)
            if suffix:
                name = f"{prefix} {adjective} {noun} {number} {suffix}"
            else:
                name = f"{prefix} {adjective} {noun} {number}"
        else:
            # Kitchen sink: prefix + adjective + noun + verb + number
            prefix = random.choice(PREFIXES)
            verb = random.choice(VERBS)
            number = random.choice(NUMBERS)
            name = f"{prefix} {adjective} {noun} {verb} {number}"
        
        if name not in used_names:
            used_names.add(name)
            return name
    
    # Fallback with index and random hash for guaranteed uniqueness
    hash_suffix = hashlib.md5(f"{index}{random.random()}".encode()).hexdigest()[:6]
    fallback_name = f"Item {index:07d}-{hash_suffix}"
    used_names.add(fallback_name)
    return fallback_name


def generate_color_from_name(name: str) -> tuple:
    """Generate a consistent color based on the filename hash."""
    hash_val = int(hashlib.md5(name.encode()).hexdigest()[:8], 16)
    hue = (hash_val % 360) / 360.0
    saturation = 0.6 + (hash_val % 40) / 100.0  # 0.6-1.0
    value = 0.7 + (hash_val % 30) / 100.0       # 0.7-1.0
    
    r, g, b = colorsys.hsv_to_rgb(hue, saturation, value)
    return (int(r * 255), int(g * 255), int(b * 255))


def generate_contrasting_color(bg_color: tuple) -> tuple:
    """Generate a contrasting color for text/QR visibility."""
    r, g, b = bg_color
    luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255
    
    if luminance > 0.5:
        return (30, 30, 30)  # Dark text on light background
    else:
        return (245, 245, 245)  # Light text on dark background


def create_artwork_image(
    name: str,
    output_path: Path,
    size: tuple = (400, 400)
) -> None:
    """Create an artwork image with QR code and filename."""
    bg_color = generate_color_from_name(name)
    fg_color = generate_contrasting_color(bg_color)
    
    # Create base image with gradient-like effect
    img = Image.new('RGB', size, bg_color)
    draw = ImageDraw.Draw(img)
    
    # Add some visual interest with a darker border/frame
    border_color = tuple(max(0, c - 40) for c in bg_color)
    border_width = 8
    draw.rectangle(
        [0, 0, size[0] - 1, size[1] - 1],
        outline=border_color,
        width=border_width
    )
    
    # Generate QR code
    qr = qrcode.QRCode(
        version=1,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=6,
        border=2,
    )
    qr.add_data(name)
    qr.make(fit=True)
    
    # Create QR code image with matching colors
    qr_img = qr.make_image(fill_color=fg_color, back_color=bg_color)
    qr_img = qr_img.convert('RGB')
    
    # Resize QR to fit nicely (about 40% of image width)
    qr_size = int(size[0] * 0.4)
    qr_img = qr_img.resize((qr_size, qr_size), Image.Resampling.NEAREST)
    
    # Position QR code in upper portion
    qr_x = (size[0] - qr_size) // 2
    qr_y = int(size[1] * 0.15)
    img.paste(qr_img, (qr_x, qr_y))
    
    # Add filename text
    # Try to use a nice font, fall back to default
    font_size = int(size[0] * 0.06)
    try:
        # Try common system fonts
        font_paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
        ]
        font = None
        for fp in font_paths:
            if os.path.exists(fp):
                font = ImageFont.truetype(fp, font_size)
                break
        if font is None:
            font = ImageFont.load_default()
    except Exception:
        font = ImageFont.load_default()
    
    # Calculate text position (centered, below QR code)
    text_y = qr_y + qr_size + int(size[1] * 0.08)
    
    # Word wrap if name is too long
    max_width = size[0] - 40
    words = name.split()
    lines = []
    current_line = []
    
    for word in words:
        test_line = ' '.join(current_line + [word])
        bbox = draw.textbbox((0, 0), test_line, font=font)
        if bbox[2] - bbox[0] <= max_width:
            current_line.append(word)
        else:
            if current_line:
                lines.append(' '.join(current_line))
            current_line = [word]
    if current_line:
        lines.append(' '.join(current_line))
    
    # Draw each line centered
    line_height = font_size + 4
    for i, line in enumerate(lines):
        bbox = draw.textbbox((0, 0), line, font=font)
        text_width = bbox[2] - bbox[0]
        text_x = (size[0] - text_width) // 2
        draw.text(
            (text_x, text_y + i * line_height),
            line,
            fill=fg_color,
            font=font
        )
    
    # Add a subtle "cover art" decoration
    # Draw corner accents
    accent_color = tuple(min(255, c + 60) for c in bg_color)
    corner_size = 20
    # Top-left
    draw.polygon([(border_width, border_width), 
                  (border_width + corner_size, border_width),
                  (border_width, border_width + corner_size)],
                 fill=accent_color)
    # Top-right
    draw.polygon([(size[0] - border_width - 1, border_width),
                  (size[0] - border_width - corner_size - 1, border_width),
                  (size[0] - border_width - 1, border_width + corner_size)],
                 fill=accent_color)
    
    # Save the image
    img.save(output_path, 'PNG')


def create_media_file(name: str, output_path: Path) -> None:
    """Create a dummy media file with some content."""
    # No longer needed - we only generate artwork now
    pass


def generate_seed_data(
    count: int,
    output_dir: Path,
) -> None:
    """Generate the artwork images."""
    # Create directory
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Generating {count} artwork images...")
    print(f"  Output folder: {output_dir}")
    print()
    
    used_names = set()
    
    for i in range(count):
        # Generate unique filename
        name = generate_filename(i, used_names)
        artwork_filename = f"{name}.png"
        
        # Create the artwork image
        create_artwork_image(name, output_dir / artwork_filename)
        
        # Progress indicator
        if (i + 1) % 10 == 0 or i == count - 1:
            print(f"  Generated {i + 1}/{count} images", end='\r')
    
    print()
    print()
    print("✓ Generation complete!")
    print()
    print("To use with Kartend, add this to your kartend.cfg:")
    print()
    print(f"[Demo Collection]")
    print(f"mediaDirectory={output_dir}")
    print(f"artworkDirectory={output_dir}")
    print(f"gridWidth=6")
    print(f"rowHeight=200")


def main():
    parser = argparse.ArgumentParser(
        description="Generate seed data for Kartend demos/presentations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --count 50
  %(prog)s --count 100 --output-dir ~/kartend-demo
  %(prog)s -n 200 -o /tmp/kartend-seed
        """
    )
    
    parser.add_argument(
        '-n', '--count',
        type=int,
        default=50,
        help='Number of items to generate (default: 50)'
    )
    
    parser.add_argument(
        '-o', '--output-dir',
        type=Path,
        default=Path.home() / 'kartend-seed-data',
        help='Output directory (default: ~/kartend-seed-data)'
    )
    
    parser.add_argument(
        '--seed',
        type=int,
        default=None,
        help='Random seed for reproducible generation'
    )
    
    args = parser.parse_args()
    
    if args.seed is not None:
        random.seed(args.seed)
    
    if args.count < 1:
        parser.error("Count must be at least 1")
    
    generate_seed_data(
        count=args.count,
        output_dir=args.output_dir,
    )


if __name__ == '__main__':
    main()
