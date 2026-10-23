-- World-space route over a finite 2 km landscape. The tour's ground clearance
-- samples authored terrain collision so ridge crossings stay above the surface.
return {
  {name = "THE LONG VALLEY", description = "Two kilometres of terrain; a six-kilometre camera range.",
   position = {-750, 120, -70}, target = {100, 85, 180}},
  {name = "MEADOW TO MOUNTAIN", description = "Cross the valley towards fractured ridges and exposed rock.",
   position = {400, 105, -260}, target = {650, 400, -650}},
  {name = "EASTERN RIDGE", description = "Nearby ridge detail replaces distant terrain LOD as the camera approaches.",
   position = {750, 650, -550}, target = {200, 180, 100}},
  {name = "ACROSS THE RANGE", description = "Look across the whole landscape; distant silhouettes remain visible.",
   position = {650, 900, 800}, target = {-550, 320, -300}},
  {name = "WESTERN GULLIES", description = "Fine ridge masks carve actual channels into the heightfield.",
   position = {-850, 850, 500}, target = {-250, 160, 150}},
  {name = "RETURN TO THE LAKE", description = "Return over the lake to revisit earlier terrain detail.",
   position = {-250, 120, 180}, target = {80, 35, -50}},
  {name = "RESIDENCY AND RETURN", description = "Press M to unload this landscape and return to Silvermere.",
   position = {-800, 720, -600}, target = {450, 170, 0}},
}
