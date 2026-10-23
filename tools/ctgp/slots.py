# SPDX-License-Identifier: GPL-3.0-or-later
"""The game's 12 cups and 48 race courses (Mario Kart 8 Deluxe 3.0.4): the course folder (romfs Course/<name>), the music name
(Audio/Resource/Stream/pBGM_<music>_N/F.bfstm), the message label of the course name (Common.msbt; +100 for the "tagged" title form)
and the cup's message label / cup-icon name. Animal Crossing (four seasonal folders sharing one name) is left out."""

# (course folder, music name, name label)
CUPS = [
    ("Mushroom", 1301, "Mushroom", [("Gu_FirstCircuit", "WU_FIRST_CIRCUIT", 1401), ("Gu_WaterPark", "WU_WATER_PARK", 1402), ("Gu_Cake", "WU_CAKE", 1403), ("Gu_DossunIseki", "WU_DOSSUN", 1404)]),
    ("Flower", 1302, "Flower", [("Gu_MarioCircuit", "WU_CIRCUIT", 1406), ("Gu_City", "WU_CITY", 1405), ("Gu_HorrorHouse", "WU_HAUNTED", 1408), ("Gu_Expert", "WU_EXPERT", 1411)]),
    ("Star", 1303, "Star", [("Gu_Airport", "WU_AIRPORT", 1409), ("Gu_Ocean", "WU_OCEAN", 1410), ("Gu_Techno", "WU_TECHNO", 1407), ("Gu_SnowMountain", "WU_SNOW_MOUNTAIN", 1412)]),
    ("Special", 1304, "Special", [("Gu_Cloud", "WU_CLOUD", 1414), ("Gu_Desert", "WU_DESERT", 1413), ("Gu_BowserCastle", "WU_BOWSER_CASTLE", 1415), ("Gu_RainbowRoad", "WU_RAINBOW_ROAD", 1416)]),
    ("Shell", 1305, "Shell", [("Gwii_MooMooMeadows", "WII_MOO_MOO_MEADOWS", 1441), ("Gagb_MarioCircuit", "AGB_MARIO_CIRCUIT", 1442), ("Gds_PukupukuBeach", "DS_PUKUPUKU_BEACH", 1443), ("G64_KinopioHighway", "N64_KINOPIO_HIGHWAY", 1445)]),
    ("Banana", 1306, "Banana", [("Ggc_DryDryDesert", "GC_DRY_DRY_DESERT", 1447), ("Gsfc_DonutsPlain3", "SFC_DONUTS_PLAIN3", 1446), ("G64_PeachCircuit", "N64_PEACH_CIRCUIT", 1451), ("G3ds_DKJungle", "3DS_DK_JUNGLE", 1448)]),
    ("Leaf", 1307, "Leaf", [("Gds_WarioStadium", "DS_WARIO_STADIUM", 1454), ("Ggc_SherbetLand", "GC_SHERBET_LAND", 1449), ("G3ds_MusicPark", "3DS_MUSIC_PARK", 1452), ("G64_YoshiValley", "N64_YOSHI_VALLEY", 1453)]),
    ("Lightning", 1308, "Thunder", [("Gds_TickTockClock", "DS_TICK_TOCK_CLOCK", 1450), ("G3ds_PackunSlider", "3DS_PACKUN_SLIDER", 1444), ("Gwii_GrumbleVolcano", "WII_GRUMBLE_VOLCANO", 1455), ("G64_RainbowRoad", "N64_RAINBOW_ROAD", 1456)]),
    ("Egg", 1309, "DLC02", [("Dgc_YoshiCircuit", "GC_YOSHICIRCUIT", 1485), ("Du_ExciteBike", "WU_EXCITEBIKE", 1482), ("Du_DragonRoad", "WU_DRAGONROAD", 1483), ("Du_MuteCity", "WU_MUTECITY", 1484)]),
    ("Triforce", 1310, "DLC03", [("Dwii_WariosMine", "WII_WARIOSMINE", 1481), ("Dsfc_RainbowRoad", "SFC_RAINBOWROAD", 1486), ("Du_IcePark", "WU_ICEPARK", 1487), ("Du_Hyrule", "WU_HYRULE", 1488)]),
    ("Crossing", 1311, "DLC04", [("Dgc_BabyPark", "GC_BABYPARK", 1490), ("Dagb_CheeseLand", "GBA_CHEESELAND", 1489), ("Du_Woods", "WU_WOOD", 1491), ("Du_Animal_Spring", "WU_ANIMALSPRING", 1492)]),
    ("Bell", 1312, "DLC05", [("D3ds_NeoBowserCity", "3DS_NEOBOWSERCITY", 1493), ("Dagb_RibbonRoad", "GBA_RIBBONROAD", 1494), ("Du_Metro", "WU_METRO", 1495), ("Du_BigBlue", "WU_BIGBLUE", 1496)]),
]

# CTGP DX's 16 cups in the order of its cup-name labels 700000..700015 and the icon of each
CTGP_CUP_ICONS = ["Boo", "Blooper", "Penguin", "Packun", "CloudFlower", "SuperCrown", "GrandStar", "MultiMoon", "SuperLeaf", "Squid",
                  "Heart", "ShineSprite", "ExtraMushroom", "IceFlower", "PranksterComet", "Coin"]
ICON_VARIANTS = ["130x130^h", "Line128x128^s", "Line^s", "Nml^l"]
LANGUAGES = ["CNzh", "EUde", "EUen", "EUes", "EUfr", "EUit", "EUnl", "EUpt", "EUru", "JPja", "KRko", "TWzh", "USen", "USes", "USfr"]

# Animal Crossing is one course with four seasonal folders: the track goes into all of them. folder -> (music, picture name)
ANIMAL_CROSSING_EXTRA = [("Du_Animal_Summer", "WU_ANIMALSUMMER"), ("Du_Animal_Autumn", "WU_ANIMALAUTUMN"), ("Du_Animal_Winter", "WU_ANIMALWINTER")]
ANIMAL_CROSSING_PICTURE = "Du_Animal"

# CTGP DX's real cup layout (its wiki, cup-major: cup k holds tracks 4k..4k+3, which are also the pack's CT_xx numbers).
# The pack's name labels are NOT in this order, so each track's name is looked up by text.
CTGP_LAYOUT = [
    "Omatsuri Circuit", "Vanilla Lake 1", "Broken Pier", "Mystic Tangle",
    "Snow Panic", "Mario Circuit 2", "Volcanic Skyway", "Whale Lagoon",
    "DK Jungle", "Frappe Snowland", "Mario Circuit", "Jungle Falls",
    "Koopa Beach 1", "Mario Circuit", "Desert Hills", "Pill Hill Valley",
    "Lakeside Park", "Wario Circuit", "Banshee Boardwalk", "Creep Sea Cape",
    "Koopa Troopa Beach", "Wario Shipyard", "Somnom Labyrinth", "Walrus Cove",
    "Koopa Beach 2", "Green Hill Zone", "Cavi Cape Cliffside", "Nabbit's Secret Island",
    "Luigi Circuit", "Choco Island 1", "Boshi's Skatepark", "Windmill Isle",
    "Sky High Cathedral", "Shipwreck Shore", "Bowser's Castle", "DK's Jungle Parkway",
    "Humpback Pump Track", "Area 64", "Cheep Cheep Cove", "Teehee Trails",
    "Fossil Canyon", "DK Pass", "Bowser Castle 4", "Rainbow Road 1",
    "Dry Dry Falls", "Donut Plains 2", "Pac Mountain", "Vacay Bay",
    "Moo Moo Mountain", "Tropical Coast", "Shy Guy Bazaar", "Bowser's Castle",
    "Kingdom Way", "Alpine Skyway", "Bowser's Castle", "Rainbow Road",
    "Choco Island 1", "Haunted Woods", "Bowser's Castle", "Rainbow Road",
    "Mushroom Bridge", "Luigi Circuit", "Delfino Square", "Piranha Plant Pipeline",
]
