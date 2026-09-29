"EXPECT: 2024-02-29 2024-03-01 2024-01-01 2023-03-01 2024-02-29 2024-01-01 2025-01-01 1900-03-01 2000-02-29 1999-12-31 29 28 30 31 28 29 3 3 2025-03-15 2025-03-15 true false true 29 -29 true 1 true true"
"Date addDays: across month, year and leap-year boundaries; newDay:month:year:; daysInMonth; month (the month index); comparison, subtraction in days, hash; Date today minus an earlier Date."
d := [:y :m :dd | Date year: y month: m day: dd].
t1 := ((d value: 2024 value: 2 value: 28) addDays: 1) printString , ' ' , ((d value: 2024 value: 2 value: 28) addDays: 2) printString , ' ' , ((d value: 2023 value: 12 value: 31) addDays: 1) printString , ' ' , ((d value: 2023 value: 2 value: 28) addDays: 1) printString , ' ' , ((d value: 2024 value: 3 value: 1) addDays: -1) printString.
t2 := ((d value: 2023 value: 1 value: 1) addDays: 365) printString , ' ' , ((d value: 2024 value: 1 value: 1) addDays: 366) printString , ' ' , ((d value: 1900 value: 2 value: 28) addDays: 1) printString , ' ' , ((d value: 2000 value: 2 value: 28) addDays: 1) printString , ' ' , ((d value: 2000 value: 1 value: 1) addDays: -1) printString.
t3 := (d value: 2024 value: 2 value: 1) daysInMonth printString , ' ' , (d value: 2023 value: 2 value: 1) daysInMonth printString , ' ' , (d value: 2023 value: 4 value: 9) daysInMonth printString , ' ' , (d value: 2023 value: 12 value: 9) daysInMonth printString , ' ' , (d value: 1900 value: 2 value: 1) daysInMonth printString , ' ' , (d value: 2000 value: 2 value: 1) daysInMonth printString.
t4 := (d value: 2025 value: 3 value: 15) month printString , ' ' , (d value: 2025 value: 3 value: 15) monthIndex printString , ' ' , (Date newDay: 15 month: 3 year: 2025) printString , ' ' , (Date newDay: 15 month: 'March' year: 2025) printString.
a := d value: 2024 value: 3 value: 1.
b := d value: 2024 value: 2 value: 1.
t5 := (b < a) printString , ' ' , (a < b) printString , ' ' , (a = (d value: 2024 value: 3 value: 1)) printString , ' ' , (a - b) printString , ' ' , (b - a) printString , ' ' , (a hash = (d value: 2024 value: 3 value: 1) hash) printString , ' ' , (Set withAll: (Array with: a with: (d value: 2024 value: 3 value: 1))) size printString.
t6 := (Date today - (Date today addDays: -10) = 10) printString , ' ' , ((Date today addDays: 0) = Date today) printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5 , ' ' , t6.
