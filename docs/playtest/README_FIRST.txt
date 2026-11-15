=====================================================================
                         F I N A L   N I G H T   7
              Playtest welcome  -  Epic AI MegaJam 2026
=====================================================================

You are a GOD at a table of gods. A globe of the real Earth floats
over the table, and below it a hex board shows the part of the world
you're looking at. Two demigods fight for you down there. You don't
press buttons to move them: you WRITE THEIR WILL, in plain words, and
time itself slows while you write. Call down volcanoes. Raise armies.
Duel face to face in typed blows. When the Final Night falls, exactly
one god is left.

  * Two demigods each: your LEFT HAND and your RIGHT HAND
  * Last god with a demigod standing wins
  * A match is meant to feel about 7 minutes
  * The world never waits: it beats on whether you type or not
  * AI gods fill every empty seat, and you can join mid-match

(There's a prettier version of this file: README_FIRST.html.
 Testing for us? See PLAYTEST_CHECKLIST.md and KNOWN_ISSUES.md.)


---------------------------------------------------------------------
 I. THREE STEPS TO THE TABLE
---------------------------------------------------------------------

 1. UNZIP
    Unzip the Windows build you were sent anywhere you like, for
    example your Desktop. Inside you'll find FinalNight.exe, an
    Engine folder and a FinalNight folder.

 2. DROP IN YOUR KEY
    You'll be sent an access key privately. It is NOT in the zip.
    Make a plain text file with that key on one line, here:

        FinalNight\Config\FN7Key.txt

    That's the FinalNight folder next to FinalNight.exe. If there's no
    Config folder in it, create one. The file holds one line:

        <the access key you were sent>

 3. LAUNCH
    Double-click FinalNight.exe. The server address is already built
    into the game, so there's nothing to type in.

 NOTEPAD TRAP: Windows hides file extensions, so Notepad can quietly
 save "FN7Key.txt.txt". Turn on View > File name extensions in
 Explorer and check.

 PREFER AN ENVIRONMENT VARIABLE? Instead of the file, run this in a
 Command Prompt, then launch the game:

        setx FN7_KEY "<the access key you were sent>"

 The game reads the file first, then the variable.

 KEEP THE KEY TO YOURSELF. Don't post it or pass it on.


---------------------------------------------------------------------
 II. YOUR FIRST MINUTE
---------------------------------------------------------------------

 * WELCOME. Pick the name the other gods will know you by (1 to 24
   characters), then press PLAY AS GUEST. No email, no password:
   this computer keeps you signed in. (If SIGN IN WITH GOOGLE is
   offered, that works too, and "play without an account" skips it.)

 * THE LOBBY. A narrator welcomes you. The lobby shows whether the
   server is online and THE HOUSE GAME: the always-on game anyone
   can drop into, with who's at the table and when the next match
   starts. Your ways in:

     JOIN THE GAME ...... take a seat in the house game
     WATCH .............. just watch it
     PLAY VS BOTS ....... your own match against AI gods
     HOST A ROOM ........ you get a ROOM code to give your friends;
                          press START when everyone is in. CANCEL
                          closes your room and takes you back to the
                          house game (so does BACK TO THE GAME)
     JOIN BY CODE ....... type a friend's room code, press JOIN

   SETTINGS, DEMIGODS and YOUR GOD in the lobby let you set volumes
   and options, write your two demigods' personalities, and shape the
   stone statue of your god at the table.

 * HOW TO PLAY. A stone tablet appears the first time you join.
   GOT IT closes it; the "?" button at the top brings it back.

 * YOUR SEAT. If a match is already running, you take over a
   demigod from the next beat on. If a new match is starting, beat 1
   is the FINAL WISH: your first orders get extra power behind them.
   Think big.

 * THE WORLD MOVES WITHOUT YOU. Beats roll on whether you type or
   not. The AI gods are already scheming. Better start writing.


---------------------------------------------------------------------
 III. THE SCREEN
---------------------------------------------------------------------

 TOP (the header)
   LOBBY ......... back to the lobby (Esc does it too)
   RESET VIEW .... the board, the camera and the panels back where
                   they were (R or Home)
   ? ............. how to play
   TREE .......... the Tree of Consequence (see section VIII)
   Also up there: the ROOM, the BEAT (e.g. 12/60), how many are
   ALIVE, and the ENTROPY meter.

 BOTTOM LEFT: your two demigod cards
   A portrait card for each demigod (L and R) with its health. Each
   card has an ITEMS button: click it to see what that demigod
   carries; click again or press Esc to close. Your purse sits here
   too: BOLTS, SPARKS and DEBT (what you owe Time for slowing it).

 Bottom right: the Will scroll
   "YOUR WILL - click to write". This is where you type. While it's
   open you get four carved icon hot buttons (see section V).

 RIGHT SIDE: the power hexes and the pouch
   Carved wooden hexes in an arc on the table: your powers. The
   crimson velvet pouch beside them pours them out or puts them away
   (it opens by itself when you join).

 THE WORLD LOG
   The WORLD scroll: what's happening, in plain sentences. The + and
   - on it fold it open or shut.

 CHAT
   A small speech-bubble icon, with a badge when there's something
   new. Click it to open the chat. Enter sends, Esc leaves.


---------------------------------------------------------------------
 IV. CONTROLS
---------------------------------------------------------------------

 THE VIEW
   Drag the globe ............... spin the world
   Click the globe .............. dive to that region
   Drag the board ............... pan: the land under your pointer
                                  stays under your pointer
   Mouse wheel .................. zoom in or out one level (over a
                                  demigod, it zooms onto it)
   + and - keys ................. zoom too
   Double-click empty board ..... zoom in one level
   Right-click .................. zoom out one level
   R, Home or RESET VIEW ........ back to the start view

 YOUR DEMIGODS AND RIVALS
   Click a demigod .............. select it
   Double-click YOUR demigod .... centre the view on it at the same
   (piece or card)                zoom, with a gold ring under it
   Double-click a RIVAL ......... in reach: the CHALLENGE card with
                                  your odds (see section VI)
                                  far away: centre the view on it
   Drag your demigod (piece or .. SUMMON it there. It flashes and
   card) onto the board or globe  lands that beat, but summoning is
                                  risky: expect to lose 1 to 2 health
                                  (it never drops you below 2.5)

 POWERS
   Drag a power onto the GLOBE .. a MEGA. Costs a BOLT. Hits on map
                                  scale, and every other god gains a
                                  bolt, so each mega arms your rivals
   Drag a power onto the BOARD .. a MINI. Costs a SPARK. Hits a small
                                  spot

   Dropping a power right on your OWN demigod? The game warns you
   ("Careful: that's YOUR OWN ...") and keeps the power in your hand.
   Drop it there again to strike anyway.

 KEYS
   Enter ........................ open the WILL / send it
   Shift+Enter .................. new line in the WILL
   Esc .......................... roll the WILL up (your text stays),
                                  close a card, the TREE or the chat,
                                  or open and close the lobby


---------------------------------------------------------------------
 V. THE WILL
---------------------------------------------------------------------

 Type what your demigods do, in plain words, then press Enter (or
 SEND). Start a line with r: for your Right Hand, l: for your Left.

 SLOWING TIME. The moment you start typing, time slows for EVERY
 player, for up to 30 seconds, and everyone is told who did it. Your
 Enter sends your will and cuts off anyone still typing. Time takes a
 toll for it: a bolt, plus a little debt.

 Name who, where and with what. Say how big. Try:
     raise an army of 10,000 fishermen at the harbour of Lagos
     arm yourself with an obsidian blade and hunt the nearest rival
     Nature, heal my Right Hand
     r: march north and topple their shrine

 THE FOUR HOT BUTTONS (icons on the open WILL)
   Ankh ....... bring back a fallen demigod (only shows when one has
                fallen)
   Bow ........ arm and hunt a rival
   Meal ....... heal your most hurt demigod
   Armies ..... raise an army
 Hover one to read its line. A press fills the WILL with that line
 and sends it after a 5-second countdown ("goes in 5 - type to make it
 yours"). Edit the line and it won't go by itself: it's your will
 then. Press another button to swap the line and restart the count.

 POWERS ARE SEPARATE. Typing never casts a power, and a power never
 carries orders. Bolts and sparks come in a 1 to 5 ratio: you start
 with 1 bolt and 5 sparks. The seven gods are Time, Nature,
 Consequence, Death, Chance, Memory and ENTROPY. Every cast pushes the
 Entropy meter up, and at 100 comes HEAT DEATH: the Final Night falls
 at once. Too many megas end the world.

 FAULT LINES. While a mega is in your hand, the planet's real
 tectonic plate boundaries glow. Megas hit far harder on a fault
 line, and hardest of all where plates meet; far from any fault they
 fizzle. Find the lines.


---------------------------------------------------------------------
 VI. DUELS
---------------------------------------------------------------------

 Double-click a rival in reach. The CHALLENGE card shows who fights
 whom and your chance to win.
   Enter or DUEL ............. fight
   Esc or Not now ............ back off
 If the rival is on your board but not in reach, the button reads
 TELEPORT + DUEL: your demigod jumps beside it first, which can cost
 1 to 2 health.

 The narrator calls "CLASH! Will against will." You have 30 seconds.
 EACH ENTER IS A BLOW: type your move and press Enter to strike. Every
 line is judged at once by the rules and a d20. Lines with an action
 verb, a body part, an item you hold or the ground you're on hit
 harder, so
     "I vault the rock and drive my knee into their ribs"
 beats "hit". Repeats, very short lines and gibberish cost you, and
 so does spamming Enter.

 ONLY WHAT'S REAL. You can use only what your demigod holds or what's
 there on the board (a tree, a rock, the mud). Name a pistol you
 don't have and the blow whiffs: "you have no pistol".

 Your rival's line shows up masked as they type, with only the newest
 letters showing. Read fast and you'll know their move.


---------------------------------------------------------------------
 VII. FALLING, AND COMING BACK
---------------------------------------------------------------------

 Each demigod has 10 health. You're out only when BOTH fall.

 Fallen? Plead to Death in your WILL:
     Death, bring back my demigods
 or press the ankh hot button, or PLEAD TO DEATH on the card that
 tells you they fell. Even a god with both demigods down can plead.
 Death names a price in sparks; when you can't pay, it sets you a
 quest. You can also WATCH, or RETURN TO LOBBY.

 Curses, seals and mystic quests are in the game too: ask the WILL
 "what does my quest need?" and go do it.


---------------------------------------------------------------------
 VIII. THE TREE OF CONSEQUENCE
---------------------------------------------------------------------

 The game remembers everything, by place and time, from the whole
 planet down to a single 3-metre hex. Press TREE in the header to see
 it as a star map: bigger nodes remember more, brighter ones are more
 recent. Click a node to see what it holds and how one fact travels
 up into what the Dungeon Master knows. CLOSE or Esc to leave.


---------------------------------------------------------------------
 IX. AUDIO
---------------------------------------------------------------------

 A narrator speaks at the big moments: the welcome, the start of a
 match, your duel's clash and its end, victory and defeat. Any key
 skips a line. The crackling in the background sound is fixed.
 Volumes (Master, Music, Sound effects, Voices) are under SETTINGS
 in the lobby.


---------------------------------------------------------------------
 X. WHAT WE WANT TO HEAR FROM YOU
---------------------------------------------------------------------

 Tell us everything, in your own words. Blunt is good. "That felt
 bad" helps us more than polite silence. PLAYTEST_CHECKLIST.md has a
 short list of checks and a template for reporting bugs.

 * The first five minutes. Did you get in? Did you know what to do?
 * Your words. Did the world do what you wrote? When it didn't, did
   the twist make sense or feel unfair?
 * The duel. Did it feel like a fight? What should a hit feel like?
 * Controls. Spinning, panning, zooming, dragging powers and
   demigods. Anything fighting you?
 * Balance. Megas, minis, summoning risk, duel length.
 * The best and worst moment. One of each, please.


---------------------------------------------------------------------
 XI. TROUBLESHOOTING
---------------------------------------------------------------------

 "CAN'T REACH THE GAME"
   If this stone card sits over the table, the game can't get to the
   server yet. It retries every few seconds by itself.

 "...this copy of the game has no access key"
   The key didn't load, or it's wrong. Check:
   * The file is exactly FinalNight\Config\FN7Key.txt, inside the
     FinalNight folder next to the exe.
   * It isn't secretly FN7Key.txt.txt.
   * It holds just the key, copied exactly (no quotes needed).
   * Restart the game after fixing it. Still no luck? Ask for the
     key again.

 "Check your internet"
   * The game connects out over a normal secure web connection (port
     443). If Windows Firewall or your antivirus asks, allow
     FinalNight.
   * A work, school or VPN network might block it. Try another
     network.
   * The server restarts itself now and then. Give it a minute.

 "Windows protected your PC"
   It's a jam build, so it isn't code-signed. Click "More info", then
   "Run anyway".

 Slow or choppy? Performance depends on your PC. Close other heavy
 programs and tell us your graphics card.

 Found a bug? Send us the log, in the game folder:
     FinalNight\Saved\Logs\FinalNight.log


=====================================================================
   The table is set. The globe is turning. Go write your will.
=====================================================================

 Made for the Epic AI MegaJam 2026 by CHRIS & BENITO.

 Plate boundaries: Bird, P. (2002), PB2002; GeoJSON by Hugo Ahlenius,
 Nordpil (ODC-BY 1.0).
 3D figures made with Meshy. Images in the game are generated.
 Voices are designed voices, no real person's voice.
 Built in Unreal Engine.
