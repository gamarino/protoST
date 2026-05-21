"EXPECT: Ada Lovelace"
"-- 04_json.st ------------------------------------------------------------"
"-- The `json` standard-library module. `JSON parse:` turns a JSON document --"
"-- into protoST values (Dictionary / Array / String / number / Boolean /   --"
"-- nil), recursively; `JSON stringify:` is the inverse. Here we parse a    --"
"-- nested document and read a deep value back out.                         --"
"-- Expected output: Ada Lovelace                                           --"

m := Import from: 'json'.

doc := m JSON parse: '{"user": {"first": "Ada", "last": "Lovelace", "id": 7}}'.
user := doc at: 'user'.

(user at: 'first') , ' ' , (user at: 'last').
