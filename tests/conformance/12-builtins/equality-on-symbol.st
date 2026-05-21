"EXPECT: true"
"§4.2 / §12.4: a Symbol is a subclass of String, so String>>= (content
equality) is inherited and #foo = #foo yields true. (D18 lists = as bound on
SmallInteger and String; via inheritance a Symbol receiver answers it too.)"
#foo = #foo.
