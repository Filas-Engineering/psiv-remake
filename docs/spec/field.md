# Field

Moving through towns, dungeons, world maps and stations: camera, movement, collision, interaction, vehicles and party talk. Enemies in the field are in [encounters.md](encounters.md). Field frame rate is [FLOW-004](game-flow.md).

## Visuals

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-001 | Every field area is full 3D, rendered in real time. No area uses illustrated backdrops or pre-rendered stills | Decided | D-VISUALS, D-STATIC |
| FLD-002 | Each of the three worlds has a distinct look and its own traversal vehicle ([FLD-060](#vehicles)) | Proposed (scope) | S-PILLAR-4 |

## Camera

The scope proposes a pulled-back, mostly fixed field camera. It does not define "mostly fixed". FLD-011 to FLD-016 are this spec's definition.

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-010 | The field camera is pulled back and mostly fixed. Closer, directed cameras are used only in battle and in events | Proposed (scope) | P-CAMERA |
| FLD-011 | Every walkable point of an area lies inside at least one authored camera volume. A volume sets the camera rule (fixed position, rail, or follow at a fixed offset), field of view, and an allowed yaw range. Where volumes overlap, the higher-priority volume wins | Proposed (spec) | P-CAMERA |
| FLD-012 | The player cannot orbit or zoom the field camera. The only player camera input is an optional nudge within the volume's yaw range, which springs back on release | Proposed (spec) | P-CAMERA |
| FLD-013 | The party leader stays inside the central 80% of the screen width and height at all times | Proposed (spec) | P-CAMERA |
| FLD-014 | Moving between camera volumes blends the camera in 0.6 seconds or less. Hard cuts happen only at doors and area transitions | Proposed (spec) | P-CAMERA |
| FLD-015 | Geometry between the camera and the party fades or dithers out. No party member is fully hidden for more than 0.25 seconds | Proposed (spec) | P-CAMERA |
| FLD-016 | Movement input is relative to the camera. When the camera changes, the previous mapping holds until the stick is released or turned more than 45 degrees, so the leader never reverses at a volume edge | Proposed (spec) | P-CAMERA |
| FLD-017 | Camera volumes for an area are locked before its environment art goes past blockout. Art is finished to close-up quality only where a locked volume can see it | Proposed (spec) | S-RISKS |

## Movement and collision

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-020 | The player moves the party leader with analog input. Other active party members follow the leader in the field and never block it. Party members do not collide with each other | Proposed (spec) | S-SYS-FIELD |
| FLD-021 | Movement speed is tuned so the time to walk an area's critical path is within 15% of the reference build's time for the same route (FLD-022) | Proposed (spec) | S-PILLAR-1, S-RISKS |
| FLD-022 | To measure: walking speed and the time to cross each town and each dungeon floor on its critical path in the reference build, with no battles (or battle time subtracted). Method: frame counting on captures, one route per area, start and end at named landmarks recorded in the area inventory. Feeds FLD-021 and FLD-042 | To measure | S-ENGINE |
| FLD-023 | Field events start from hooks. A hook is placed in an area and fires on interact, on entering a trigger volume, on area load, or when a flag condition becomes true. Each hook runs one event script ([EVT-001](events-and-live-panels.md)). The game has at least the 128 hooks counted in the scope | Proposed (scope) | S-COUNTS, S-ENGINE |
| FLD-024 | The leader collides with the world through a capsule. There is no jump. No reachable position lets the leader get stuck, fall out of the world, or leave the navigable area | Proposed (spec) | S-SYS-FIELD |
| FLD-025 | NPCs collide with the leader but never trap it. An NPC that blocks a one-person-wide passage for more than 2 seconds steps aside | Proposed (spec) | S-SYS-FIELD |

## Interaction

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-030 | Interactables are: NPCs, chests, doors, signs, shops, inns, event objects, and vehicles. An interaction prompt shows when the leader is within 1.5 m and facing within 60 degrees of one. One button interacts | Proposed (spec) | S-SYS-FIELD |
| FLD-031 | When several interactables qualify, the nearest one inside the facing cone wins | Proposed (spec) | S-SYS-FIELD |
| FLD-032 | Each chest has a stable ID and contents from the source data. Its opened state is saved and never resets. The game has the 211 chests counted in the scope, subject to the phase 0 audit | Proposed (scope) | S-COUNTS |
| FLD-033 | Each shop has a stable ID and an inventory from the source data. The game has the 50 shops counted in the scope. Shop screens are in [UI-009](menus-and-ui.md) | Proposed (scope) | S-COUNTS |
| FLD-034 | Rest at an inn or equivalent calls the `core/` rest operation for every party member: HP, technique points and skill uses refill. Status cures and cost follow FLD-035 | Proposed (spec) | L-CORE |
| FLD-035 | To measure: what rest does in the original: which statuses it cures, whether it revives, its cost and how cost scales, and whether any other action refills skill uses. Method: rights holder data, cross-checked by resting with each status applied in the reference build. Feeds FLD-034 | To measure | L-CORE |

## Towns

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-040 | Every original town is rebuilt as a full 3D space. It keeps every building, shop, inn, exit and NPC of the original, in the same relative arrangement | Proposed (scope) | S-CONTENT, S-GOALS |
| FLD-041 | To measure: an inventory per town of buildings, interiors, shops, NPCs, exits and event hooks, with their relative positions. Method: rights holder maps, cross-checked against the reference build's map entries (417 per the scope). Feeds FLD-040 | To measure | S-COUNTS |

## Dungeons

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-042 | Dungeon layouts are redesigned. Each keeps the original's landmarks in the same order along the critical path, and its critical-path traversal time meets FLD-021 | Proposed (scope) | S-CONTENT |
| FLD-043 | To measure: per dungeon, the number of floors, the landmark list (entrances, chests, event points, branches, boss room) in critical-path order, and every mechanism or puzzle with its rule. Method: rights holder maps and data, cross-checked by a reference build playthrough. Feeds FLD-042 and FLD-044 | To measure | S-CONTENT |
| FLD-044 | Every dungeon mechanism or puzzle in the original keeps its logic (inputs, conditions, outcome). Its presentation may change | Proposed (spec) | S-GOALS |

## World maps and stations

The scope leaves open whether worlds are a scaled overworld or connected zones (S-CONTENT). FLD-050 proposes one.

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-050 | Each of the three planetary surfaces is a set of connected zones, not a single open overworld. Zone boundaries sit where the original's routes narrow. The space stations are areas of the same kind | Proposed (spec) | S-CONTENT, S-NONGOALS |
| FLD-051 | Fast travel exists only where the original already has it, with the same sources, destinations and conditions | Proposed (scope) | S-SYS-QOL |
| FLD-052 | To measure: every fast-travel mechanism in the original (techniques, items, transport services, automatic returns) with its allowed sources, destinations and conditions. Method: rights holder data, cross-checked in the reference build. Feeds FLD-051 | To measure | S-SYS-QOL |

## Vehicles

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-060 | There are three vehicles. Each has its own field movement and its own vehicle battles ([BAT-140](battle.md)) | Proposed (scope) | S-CONTENT, S-PILLAR-4 |
| FLD-061 | Each vehicle keeps the original's traversal rules: what terrain it crosses, what blocks it, where it can be boarded and left, and its speed relative to walking (FLD-062) | Proposed (spec) | S-GOALS |
| FLD-062 | To measure: per vehicle, its speed, passable and blocking terrain, boarding and exit rules, and the areas it reaches. Method: rights holder data, cross-checked in the reference build with timed routes. Feeds FLD-061 | To measure | S-CONTENT |
| FLD-063 | A vehicle's position is saved. If a vehicle would end up somewhere the party cannot reach, it returns to its last valid boarding point | Proposed (spec) | S-SYS-FIELD |

## Party talk

The scope keeps party talk and says it is "expanded with existing script lines only". It does not say how. FLD-073 is this spec's proposal for the expansion.

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLD-070 | Party talk is available from one button in the field whenever no event is running. It shows a conversation chosen for the current context | Proposed (scope) | S-PILLAR-3 |
| FLD-071 | Party talk uses only lines from the shipped dialogue. No new line is written for it | Decided | D-DIALOGUE |
| FLD-072 | Party talk sets are keyed by area and a story flag range. The most specific match wins. If nothing matches, the set for the current story chapter plays | Proposed (spec) | S-PILLAR-3 |
| FLD-073 | Expansion: existing party talk lines can also fire as ambient, non-blocking text when the party enters an area for the first time in a story chapter. Ambient talk never takes control from the player and can be turned off ([UI-013](menus-and-ui.md)) | Proposed (spec) | S-PILLAR-3 |
| FLD-074 | To measure: where the original allows party talk, how it picks the conversation, and how many conversation sets exist. Method: rights holder script and data; cross-check by triggering party talk in every area of a reference build playthrough. Feeds FLD-070 and FLD-072 | To measure | S-PILLAR-3 |
