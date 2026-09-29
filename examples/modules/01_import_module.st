"EXPECT: 24"
"-- 01_import_module.st ----------------------------------------------------"
"-- Modules. `Import from:` loads a `.st` file as a module and returns the  --"
"-- module object; the classes it declared are reached as attributes        --"
"-- (`geometry Rectangle`). The module is looked up in the current        --"
"-- directory, so run this example from examples/modules/.                 --"
"-- Expected output: 24  (area of a 4x6 rectangle)                          --"

geometry := Import from: '_geometry'.

rect := geometry Rectangle new.
rect setWidth: 4 height: 6.
rect area displayNl.
