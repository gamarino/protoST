"Módulo para el ejercicio 9. Un archivo .st es un módulo: al importarlo se
 corren sus formas de primer nivel una sola vez y exporta sus clases, salvo
 las que empiezan con _."

Transcript showCr: '   (cargando geometria.st)'.

Object subclass: #_Ayudante

_Ayudante class >> cuadrado: x
  ^ x * x

Object subclass: #Circulo
  instanceVariableNames: 'radio'

Circulo class >> radio: r
  ^ self new setRadio: r

Circulo >> setRadio: r
  radio := r

Circulo >> area
  ^ (_Ayudante cuadrado: radio) * 355 / 113
