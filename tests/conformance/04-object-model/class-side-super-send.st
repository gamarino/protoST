"EXPECT: #('Ac+Bc+Cc' 'Ai+Bi+Ci' 'Ac+Cc' 'Ai')"
"§4.6 + §4.7, K14: super on each side follows that side. A class-side
 method's super send reaches the superclass's class-side method of the same
 selector, and an instance-side method's super send the superclass's
 instance-side one, although every class defines the selector on both sides.
 A class that skips a side inherits it: D's class-side super skips C's
 instance-side z and finds A's class-side z, and an instance of D reaches
 A's instance-side z."
Object subclass: #K14SA.
K14SA >> z  ^ 'Ai'.
K14SA class >> z  ^ 'Ac'.
K14SA subclass: #K14SB.
K14SB >> z  ^ super z , '+Bi'.
K14SB class >> z  ^ super z , '+Bc'.
K14SB subclass: #K14SC.
K14SC class >> z  ^ super z , '+Cc'.
K14SC >> z  ^ super z , '+Ci'.
K14SA subclass: #K14SD.
K14SD class >> z  ^ super z , '+Cc'.
{ K14SC z. K14SC new z. K14SD z. K14SD new z }
