"-- _geometry.st: a module exporting geometry classes ----------------------"
"--                                                                        --"
"-- A `.st` file is a module. Every non-`_`-prefixed class it declares is   --"
"-- exposed as an attribute of the module object that `Import from:`        --"
"-- returns. This file is itself `_`-prefixed so the example runner treats  --"
"-- it as an importable library, not a standalone test.                     --"

Object subclass: #Rectangle instanceVariableNames: 'width height'.

Rectangle >> setWidth: w height: h
  width := w.
  height := h.
  ^ self.

Rectangle >> area
  ^ width * height.

Rectangle >> perimeter
  ^ 2 * (width + height).

Object subclass: #Circle instanceVariableNames: 'radius'.

Circle >> setRadius: r
  radius := r.
  ^ self.

Circle >> area
  "Float pi is a class-side constant on Float."
  ^ Float pi * radius squared.
