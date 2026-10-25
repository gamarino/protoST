"EXPECT: #(45 78 'abcdefghij')"
"§5.4: a keyword message may have any number of keyword parts."
Object subclass: #M.
M >> a: a b: b c: c d: d e: e f: f g: g h: h i: i
  ^ a + b + c + d + e + f + g + h + i.
M >> a: a b: b c: c d: d e: e f: f g: g h: h i: i j: j k: k l: l
  ^ a + b + c + d + e + f + g + h + i + j + k + l.
M >> s: a t: b u: c v: d w: e x: f y: g z: h q: i r: j
  ^ a , b , c , d , e , f , g , h , i , j.
{ M new a: 1 b: 2 c: 3 d: 4 e: 5 f: 6 g: 7 h: 8 i: 9.
  M new a: 1 b: 2 c: 3 d: 4 e: 5 f: 6 g: 7 h: 8 i: 9 j: 10 k: 11 l: 12.
  M new s: 'a' t: 'b' u: 'c' v: 'd' w: 'e' x: 'f' y: 'g' z: 'h' q: 'i' r: 'j' }
