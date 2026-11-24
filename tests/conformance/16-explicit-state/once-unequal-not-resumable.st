"EXPECT: #('IllegalResumeAttempt' 'alpha-key' 'alpha-key' 1 5 'alpha-key')"
"Spec §6.3, M12 (Task 4 review, fix round 1): ModificationForbidden is not
 resumable, so resume: on M12 raises IllegalResumeAttempt and the store never
 answers a resumption value. retry reruns the protected block (here with an
 equal value, which is silent), and return: answers its argument from on:do:.
 The variable keeps its first value throughout."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key

| o resumed candidate attempts retried returned |
o := Holder new.
o setKey: 'alpha-key'.
resumed := [[o setKey: 'beta-key'] on: ModificationForbidden do: [:e | e resume: 7]]
  on: IllegalResumeAttempt do: [:e | 'IllegalResumeAttempt'].
candidate := 'beta-key'.
attempts := 0.
retried := [o setKey: candidate]
  on: ModificationForbidden
  do: [:e | attempts := attempts + 1. candidate := 'alpha-' , 'key'. e retry].
returned := [o setKey: 'gamma-key'] on: ModificationForbidden do: [:e | e return: 5].
{ resumed. o key. retried. attempts. returned. o key }
