"EXPECT: 18 refused, message names the text: true"
"§13 stdlib: more Decimal refusals. Malformed text (a lone sign, a lone
 point, a truncated or doubled exponent, a broken Infinity, a NaN payload
 with a point, a doubled sign, the whitespace and underscores Python accepts
 but the brief's grammar does not, a non-ASCII digit), a non-String argument,
 out-of-range constructor arguments, and a second call of the private
 initialiser on a live value (immutability) all signal Error. The message of a
 refused string names it."
| m D n msg |
m := Import from: 'decimal'.
D := m Decimal.
n := 0.
#('e5' '.' '+' 'Infinit' 'Infinityx' 'sNaN1.0' '1e+' '--1' ' 1' '1 ' '1_0' '١') do: [:t |
    [D fromString: t] on: Error do: [:e | n := n + 1]].
[D fromString: 42] on: Error do: [:e | n := n + 1].
[D sign: 2 coefficient: 1 exponent: 0] on: Error do: [:e | n := n + 1].
[D sign: 0 coefficient: -1 exponent: 0] on: Error do: [:e | n := n + 1].
[D infinity: -1] on: Error do: [:e | n := n + 1].
[D nan: 1 signaling: 3 sign: 0] on: Error do: [:e | n := n + 1].
[(D fromString: '1.5') _setSign: 1 coefficient: 7 exponent: 0 kind: #finite] on: Error do: [:e | n := n + 1].
msg := [D fromString: '1..2'. ''] on: Error do: [:e | e messageText].
n printString , ' refused, message names the text: ' , (msg includesSubstring: '1..2') printString.
