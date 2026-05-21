"EXPECT: hero+skill-act"
"§4: T3-b — `super` walks the FULL resolution order: when the primary
superclass does not define the selector, `super` reaches it in a `uses:`
mixin subtree."
Object subclass: #Plain.
Object subclass: #Skill.
Skill >> act  ^ 'skill-act'.
Plain subclass: #Hero uses: { Skill }.
Hero >> act  ^ 'hero+', super act.
Hero new act.
