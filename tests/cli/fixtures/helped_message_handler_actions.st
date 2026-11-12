"With one worker: the holder actor handles a Warning and, inside the handler
 (nested in a collect: block, so the wait cannot suspend), waits on the
 helper actor, whose message therefore runs on the same thread while the
 holder's handler block is still running (waitHelping). The helper receives
 the holder's exception instance and sends it resume: and outer. Neither may
 reach the holder's handler: the helped message is another actor's turn, so
 the exception has no active handler there, and each send raises an Error
 the helper catches."
Object subclass: #Holder.
Holder >> handle: helper
  ^ [Warning signal: 'w'. 'body']
      on: Warning
      do: [:e | #(1) collect: [:x | (helper touch: e) wait]].
Object subclass: #Helper.
Helper >> touch: anException
  ^ {[anException resume: 1. 'resumed'] on: Error do: [:err | err messageText].
     [anException outer printString] on: Error do: [:err | err messageText]}.

| holder helper |
holder := Holder new asActor.
helper := Helper new asActor.
(holder handle: helper) wait
