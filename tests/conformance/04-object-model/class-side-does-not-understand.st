"EXPECT: #('class dnu: frob' 'instance dnu: frob')"
"§4.7 + D3, K14: doesNotUnderstand: is looked up like any other selector,
 so a class may define it on both sides: a message the class object does
 not understand reaches the class-side one, a message an instance does not
 understand the instance-side one."
Object subclass: #K14D.
K14D >> doesNotUnderstand: aMessage  ^ 'instance dnu: ' , aMessage selector.
K14D class >> doesNotUnderstand: aMessage  ^ 'class dnu: ' , aMessage selector.
{ K14D frob. K14D new frob }
