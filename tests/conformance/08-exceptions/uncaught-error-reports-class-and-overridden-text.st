"EXPECT-ERROR: error: MyErr: computed text"
"An unhandled exception reports its class (when it is not a plain Error) and
 the messageText the exception answers, honouring an override."
Error subclass: #MyErr.
MyErr >> messageText  ^ 'computed text'.

MyErr new signal
