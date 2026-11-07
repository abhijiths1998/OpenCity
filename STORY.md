# OpenCity: Story

Status: **draft for your review.** Working title: **"La Cala"**. All names, people, places and events are original. Nothing is taken from *The Godfather*, the *Mafia* games, GTA or any other existing work.

## Premise

**Aurelio Castrovanni**, 61, once ran the old port of Marevia. Twelve years ago he gave it up in a quiet deal: no war, no prison, a clean exit to an olive farm in the hills inland. His son Matteo stayed in the city and ran the family restaurant, *La Cala*, on the waterfront. It was the one legitimate thing the Castrovanni name ever built.

One night in early autumn, La Cala burns down with Matteo, his wife Elena and their nine-year-old daughter Giulia inside. The police call it a gas leak.

At the funeral, Aurelio is handed a melted brass key. It is the key to the restaurant's back door, and the back door was locked from the outside.

He comes back to Marevia to find out who did it, and to make them pay.

## Tone

- **A grounded crime drama** about grief, pride and the cost of revenge.
- **Dry humour** comes from the people around Aurelio, never at the victims' expense.
- **No glamour:** violence has consequences, and the story never treats the old life as a fantasy.
- **Rating:** intended for adults (18+), as is usual for the genre.
- **Aurelio** is not a hero. He is a man who has decided he has nothing left to lose, and the story keeps asking whether he's right.

## Main characters

| Character | Role | In brief |
|---|---|---|
| **Aurelio Castrovanni** | Player character | Former boss of the old port. Patient, polite and frightening when he stops being polite. Out of practice, and his body reminds him. |
| **Nina Sorrentino** | Ally, driver | Matteo's former head waiter and secretly his bodyguard, hired by Aurelio years ago without Matteo knowing. Survived the fire because she was sent on an errand that night. Blames herself. |
| **Beppe "il Ragioniere" Fava** | Ally, fixer | Aurelio's old accountant, now running a scrapyard and garage in Le Fornaci. Knows where the money went. Handles vehicles, papers and safehouses. |
| **Don Ottavio Salvi** | Antagonist | Aurelio's former right hand, who took over the port in the deal. Now runs it as a "logistics group" with a harbour-authority licence and friends in city hall. |
| **Commissario Laura Mancuso** | Wild card | The detective who closed the fire as an accident, and doesn't believe it either. Wants Salvi; doesn't want Aurelio starting a war in her city. |
| **Dario Castrovanni** | Hidden antagonist | Aurelio's nephew, the family's polite modern face. Grieves loudly beside Aurelio. Revealed at the end of Act 2. |
| **Rocco "Tre Dita" Lanza** | Enforcer | Salvi's fixer, who ran the crew that set the fire. The first name Aurelio learns. |

## Factions

- **The Salvi group:** controls the container terminal, the fishing co-op and the nightlife on the Lungomare. Modern, connected and careful.
- **The Castrovanni remnant:** a handful of loyal old men, a scrapyard and a lot of favours owed. This is Aurelio's starting position.
- **City police:** mostly indifferent, some on Salvi's payroll, and Mancuso. This drives the heat system (M7).
- **The Hill families:** old money in the villas above the city. They funded the deal twelve years ago and want quiet. This is a late-game reveal.

## Places in Marevia

The story sets the 2×2 km layout. Water splits the city into separate pieces, and the bridges between them are the gates (see "Unlocking the city" below). M3 builds all of this in grey-box.

```
                 N   LE COLLINE (hills, villas)
   ┌──────────────────────────╥───────────────────────────┐
   │  Villa Salvi · lookout   ║   olive terraces          │
   │        ╲ coast road      ║  ← Fiume Ardo (river)      │
   │         ╲          checkpoint (hills gate)            │
   │ CITTÀ ALTA        [Ponte San Rocco]    LE FORNACI    │
   │ old town, church  ═══════╬═══════════  kilns, scrap  │
   │                          ║                           │
   │ PORTO VECCHIO            ║             LUNGOMARE     │
   │ docks, fish market       ║  [Ponte Mobile]  clubs    │
   │   ~~~ DARSENA ~~~   ═════╬═══════════  beach road    │
   │ (inner harbour)          ║ river mouth / estuary     │
   │   [swing bridge]         ║                           │
   │   MOLO SALVI (terminal)  ║                           │
   └────────────────────── sea ───────────────────────────┘
```

### Water

| Water | What it is | Gameplay |
|---|---|---|
| **Fiume Ardo** | A river about 50 m wide, from the hills down to the sea, splitting the west bank (Città Alta, Porto Vecchio) from the east (Le Fornaci, Lungomare). | Two road bridges plus a footbridge: chokepoints for chases and police roadblocks (M7). |
| **Estuary** | The river mouth widening into a sandy delta where it meets the beach. | Shallow edges you can wade; a scenic shortcut on foot. |
| **Darsena** | The walled inner harbour, cutting into Porto Vecchio, full of moored fishing boats. | Separates the Salvi Terminal pier from the rest of the port. |
| **The sea** | The southern edge of the map. | Map boundary. Boats are a possible post-v1 feature. |

Deep water (river channel, Darsena, open sea) is not swimmable in v1. Falling in fades out and puts you back on the nearest bank. Vehicles that drive in are wrecked.

### Districts

| District | Feel | Story landmarks |
|---|---|---|
| **Porto Vecchio**: old port (west bank, south) | Cranes, warehouses, fish market, ferry pier. | **Fish market**, **the burned shell of La Cala** on the waterfront. |
| **Molo Salvi**: terminal pier (across the Darsena) | Container stacks, gantry cranes, Salvi's glass office tower. | **Salvi Terminal**. |
| **Città Alta**: old town (west bank, north) | Narrow streets, steep stairs, small squares, churches. | **Santa Lucia church and cemetery**, **Castrovanni townhouse**, **police headquarters**. |
| **Le Fornaci**: industrial quarter (east bank, north) | Brick chimneys, disused rail yard, scrap. | **Beppe's scrapyard and garage** (safehouse), **the abandoned kiln**. |
| **Lungomare**: beach road (east bank, south) | Promenade, beach clubs, hotels, neon at night. | **Club Medusa**, **Hotel Riviera**. |
| **Le Colline**: hills (north edge) | Villas among olive terraces, one winding road down to the coast. | **Villa Salvi**, **the coast road**, **the lookout**. |

## Unlocking the city

You start with a small part of Marevia. Each locked area sits behind a gate with a reason that comes from the story. A gate opens when the story reaches a point named in the table.

| Area | Gate while locked | In-story reason | Opens after |
|---|---|---|---|
| **Città Alta** | — | Start | Open from the start |
| **Le Fornaci** (via Ponte San Rocco) | — | The road home from the funeral | Open from the start ("Funerale" drives across it) |
| **Porto Vecchio** | Police cordon across the port road | The fire scene at La Cala is still taped off | Mission **"Funerale"** |
| **Le Colline** | Private-road checkpoint, Hill families' security | Old money keeps the hills quiet | Opens *for* mission **"Strada Costiera"**, and stays open after |
| **Lungomare** (via Ponte Mobile) | Lifting bridge kept raised | Salvi's port authority "maintenance" keeps Aurelio off his strip | Mission **"La Fornace"** (Act 1 finale: Mancuso forces it open) |
| **Molo Salvi** (via the swing bridge) | Swing bridge turned open | Salvi controls the harbour | Start of **Act 2** |
| **Interiors**: Villa Salvi, Hotel Riviera, the townhouse | Locked doors | — | Their story missions |
| **Everything** | — | — | After the ending (free roam) |

If you get round a gate, for example by wading the estuary on foot, you don't hit an invisible wall. You get a warning ("Salvi's territory. Not yet."), and if you stay too long the area pushes back: before M7, a fade back to the last open area; from M7 on, police heat. The city stays honest about where you are without feeling walled in.

## Structure

### Prologue: The Key
The funeral at Santa Lucia. Beppe gives Aurelio the key. Leaving the cemetery, a Salvi car follows them down through the old town. *(Tutorial: walking, the camera, then driving.)*

### Act 1: Coming Home
Aurelio re-opens the townhouse, finds Nina, and rebuilds a small crew. He works out that the fire was set and traces it to Rocco Lanza's crew through the fish market.

*Ends with* Rocco cornered at the abandoned kiln. He names Salvi, then is shot from a distance before he can say more.

### Act 2: The Port
Aurelio takes Salvi apart piece by piece: Club Medusa's money, the terminal's smuggling, Salvi's friends in city hall. Mancuso starts trading information with him, uneasily.

*Ends with* Salvi, finally cornered, swearing he ordered a warning: a fire after closing time. He says someone changed the timing, and that someone locked the door.

### Act 3: Family
The evidence leads to Dario. He wanted Matteo's clean restaurant name to front a hotel deal with the Hill families, and Matteo said no. Aurelio has to decide what revenge on his own blood means.

### Ending
A confrontation at the lookout above the bay. There are two endings, chosen by the player:
- **Revenge:** Aurelio kills Dario. He keeps the city and loses himself.
- **Justice:** Aurelio hands Dario and the evidence to Mancuso, and gives up any claim to the port.

There's no ending where everyone is happy.

## How it fits the systems

| System | Story use |
|---|---|
| M5 city life | The Lungomare and the port are busy at night; Città Alta is quiet. |
| M7 heat | Mancuso's investigation makes heat persist longer in some acts. |
| M8 missions | See below. |
| M9 phone | Calls from Nina and Beppe, plus texts from Mancuso. |
| M10 economy | Rebuilding the family's businesses funds the war; Beppe's garage handles paint and repairs. |
| Radio | Talk shows react to story events, such as "Fire on the waterfront ruled an accident". |

## The three M8 sample missions

| Mission | Type | Summary |
|---|---|---|
| **"Funerale"** | Drive | Prologue. Drive Beppe home from the cemetery while a Salvi car follows; lose it in the old town without drawing police attention. |
| **"Strada Costiera"** | Chase | Act 1. Chase a terminal driver who saw the arson crew, from the port up the coast road, before he reaches Villa Salvi. |
| **"La Fornace"** | Shootout | Act 1 finale. Fight through Rocco's crew in the abandoned kiln. Ends with the sniper shot. |

## Originality checklist

- Names, factions and places are invented. "Marevia" and every business name are fictional.
- No plot beats, characters or lines taken from existing films or games.
- No real cities, real crime families, real politicians or real brands.
