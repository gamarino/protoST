Object subclass: #Box
  instanceVariableNames: 'w h'.

Box >> w: aW
  w := aW. h := aW.

Box >> w: aW h: aH
  w := aW. h := aH.

Box >> area
  ^ w * h.

Box >> scaled(factor, offset = 0)
  ^ (w * factor) + offset.

(Box new w: 3) area printNl.
(Box new w: 3 h: 4) area printNl.
((Box new w: 2) scaled(10)) printNl.
((Box new w: 2) scaled(10, offset = 1)) printNl.
