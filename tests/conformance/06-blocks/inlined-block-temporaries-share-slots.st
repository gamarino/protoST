"EXPECT: #(12000 0)"
"The temporaries of inlined blocks are hidden slots of the enclosing frame;
 blocks that cannot be active at the same time (siblings) share them, so a
 method with sixty inlined arms of one temporary each has a frame barely
 larger than without them and recurses as deep (0.10.0, fix round 1: each
 arm had a slot of its own, which halved the recursion depth this method
 reaches before 'stack depth exceeded')."
Object subclass: #W.
W >> deep: n
  | s |
  s := 0.
  n < 0 ifTrue: [ | u0 | u0 := 0. s := u0 ].
  n < 0 ifTrue: [ | u1 | u1 := 1. s := u1 ].
  n < 0 ifTrue: [ | u2 | u2 := 2. s := u2 ].
  n < 0 ifTrue: [ | u3 | u3 := 3. s := u3 ].
  n < 0 ifTrue: [ | u4 | u4 := 4. s := u4 ].
  n < 0 ifTrue: [ | u5 | u5 := 5. s := u5 ].
  n < 0 ifTrue: [ | u6 | u6 := 6. s := u6 ].
  n < 0 ifTrue: [ | u7 | u7 := 7. s := u7 ].
  n < 0 ifTrue: [ | u8 | u8 := 8. s := u8 ].
  n < 0 ifTrue: [ | u9 | u9 := 9. s := u9 ].
  n < 0 ifTrue: [ | u10 | u10 := 10. s := u10 ].
  n < 0 ifTrue: [ | u11 | u11 := 11. s := u11 ].
  n < 0 ifTrue: [ | u12 | u12 := 12. s := u12 ].
  n < 0 ifTrue: [ | u13 | u13 := 13. s := u13 ].
  n < 0 ifTrue: [ | u14 | u14 := 14. s := u14 ].
  n < 0 ifTrue: [ | u15 | u15 := 15. s := u15 ].
  n < 0 ifTrue: [ | u16 | u16 := 16. s := u16 ].
  n < 0 ifTrue: [ | u17 | u17 := 17. s := u17 ].
  n < 0 ifTrue: [ | u18 | u18 := 18. s := u18 ].
  n < 0 ifTrue: [ | u19 | u19 := 19. s := u19 ].
  n < 0 ifTrue: [ | u20 | u20 := 20. s := u20 ].
  n < 0 ifTrue: [ | u21 | u21 := 21. s := u21 ].
  n < 0 ifTrue: [ | u22 | u22 := 22. s := u22 ].
  n < 0 ifTrue: [ | u23 | u23 := 23. s := u23 ].
  n < 0 ifTrue: [ | u24 | u24 := 24. s := u24 ].
  n < 0 ifTrue: [ | u25 | u25 := 25. s := u25 ].
  n < 0 ifTrue: [ | u26 | u26 := 26. s := u26 ].
  n < 0 ifTrue: [ | u27 | u27 := 27. s := u27 ].
  n < 0 ifTrue: [ | u28 | u28 := 28. s := u28 ].
  n < 0 ifTrue: [ | u29 | u29 := 29. s := u29 ].
  n < 0 ifTrue: [ | u30 | u30 := 30. s := u30 ].
  n < 0 ifTrue: [ | u31 | u31 := 31. s := u31 ].
  n < 0 ifTrue: [ | u32 | u32 := 32. s := u32 ].
  n < 0 ifTrue: [ | u33 | u33 := 33. s := u33 ].
  n < 0 ifTrue: [ | u34 | u34 := 34. s := u34 ].
  n < 0 ifTrue: [ | u35 | u35 := 35. s := u35 ].
  n < 0 ifTrue: [ | u36 | u36 := 36. s := u36 ].
  n < 0 ifTrue: [ | u37 | u37 := 37. s := u37 ].
  n < 0 ifTrue: [ | u38 | u38 := 38. s := u38 ].
  n < 0 ifTrue: [ | u39 | u39 := 39. s := u39 ].
  n < 0 ifTrue: [ | u40 | u40 := 40. s := u40 ].
  n < 0 ifTrue: [ | u41 | u41 := 41. s := u41 ].
  n < 0 ifTrue: [ | u42 | u42 := 42. s := u42 ].
  n < 0 ifTrue: [ | u43 | u43 := 43. s := u43 ].
  n < 0 ifTrue: [ | u44 | u44 := 44. s := u44 ].
  n < 0 ifTrue: [ | u45 | u45 := 45. s := u45 ].
  n < 0 ifTrue: [ | u46 | u46 := 46. s := u46 ].
  n < 0 ifTrue: [ | u47 | u47 := 47. s := u47 ].
  n < 0 ifTrue: [ | u48 | u48 := 48. s := u48 ].
  n < 0 ifTrue: [ | u49 | u49 := 49. s := u49 ].
  n < 0 ifTrue: [ | u50 | u50 := 50. s := u50 ].
  n < 0 ifTrue: [ | u51 | u51 := 51. s := u51 ].
  n < 0 ifTrue: [ | u52 | u52 := 52. s := u52 ].
  n < 0 ifTrue: [ | u53 | u53 := 53. s := u53 ].
  n < 0 ifTrue: [ | u54 | u54 := 54. s := u54 ].
  n < 0 ifTrue: [ | u55 | u55 := 55. s := u55 ].
  n < 0 ifTrue: [ | u56 | u56 := 56. s := u56 ].
  n < 0 ifTrue: [ | u57 | u57 := 57. s := u57 ].
  n < 0 ifTrue: [ | u58 | u58 := 58. s := u58 ].
  n < 0 ifTrue: [ | u59 | u59 := 59. s := u59 ].
  n = 0 ifTrue: [ ^ 0 ].
  ^ (self deep: n - 1) + 1
{ W new deep: 12000. W new deep: 0 }
