"EXPECT: 11 1 0|7 2 1|6 3"
"§8.4 / §8.5: ex outer inside an ensure: / ifCurtailed: block in a handler.
   1. The enclosing handler resumes: outer answers normally, the ensure:
      block runs once and the ifCurtailed: block does not run.
   2. The enclosing handler returns: the unwind curtails both blocks, so
      both run.
   3. ex outer in the cleanup block of an ensure: answers too."
| ens cur r1 r2 r3 |
ens := 0.
cur := 0.
r1 := [[(Warning signal: 'w') + 1]
         on: Warning
         do: [:e | | v |
           v := [[e outer] ifCurtailed: [cur := cur + 1]] ensure: [ens := ens + 1].
           e resume: v * 10]]
       on: Warning do: [:e | e resume: 1].
r1 := r1 printString , ' ' , ens printString , ' ' , cur printString.
r2 := [[(Warning signal: 'w') + 1]
         on: Warning
         do: [:e | | v |
           v := [[e outer] ifCurtailed: [cur := cur + 1]] ensure: [ens := ens + 1].
           e resume: v * 10]]
       on: Warning do: [:e | e return: 7].
r2 := r2 printString , ' ' , ens printString , ' ' , cur printString.
r3 := [[(Warning signal: 'w') + 1]
         on: Warning
         do: [:e | | v |
           [ens := ens + 1] ensure: [v := e outer].
           e resume: v]]
       on: Warning do: [:e | e resume: 5].
r3 := r3 printString , ' ' , ens printString.
r1 , '|' , r2 , '|' , r3
