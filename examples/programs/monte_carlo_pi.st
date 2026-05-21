"EXPECT: pi-estimate-ok"
"-- monte_carlo_pi.st ------------------------------------------------------"
"-- A complete small program: a Monte-Carlo estimate of pi, combining the   --"
"-- `random` standard-library module with the `Number` math protocol.       --"
"--                                                                        --"
"-- Throw N random darts at the unit square [0,1) x [0,1). The fraction     --"
"-- landing inside the quarter unit circle approaches pi/4, so 4 * that     --"
"-- fraction approaches pi. With a fixed seed the run is fully               --"
"-- reproducible; we assert the estimate lands near the real pi.            --"

Object subclass: #PiEstimator instanceVariableNames: 'rng'.

PiEstimator >> withGenerator: aRandom
  rng := aRandom.
  ^ self.

PiEstimator >> estimateWith: samples
  | inside |
  inside := 0.
  1 to: samples do: [ :i |
    | x y |
    x := rng next.
    y := rng next.
    "Inside the quarter circle when x^2 + y^2 <= 1."
    ((x squared + y squared) <= 1.0) ifTrue: [ inside := inside + 1 ] ].
  ^ 4.0 * inside asFloat / samples asFloat.

random := Import from: 'random'.
estimator := PiEstimator new withGenerator: (random Random seed: 2026).

estimate := estimator estimateWith: 20000.

"The estimate must land within 0.1 of the true pi for this seed/sample size."
((estimate - Float pi) abs < 0.1)
  ifTrue: [ 'pi-estimate-ok' ]
  ifFalse: [ 'pi-estimate-off: ' , estimate printString ].
