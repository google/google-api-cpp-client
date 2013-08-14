#! /usr/bin/python
"""INPUT_FILTER for Doxygen that converts C++ comments to Doxygen comments."""
import sys
import re


def Doxygenate(text):
  """Change commenting style to the one recognized by Doxygen."""
  # /* -> /**
  text = re.sub(r'(/\*)([ \t\n]) ?', r'/**\2', text)

  # // -> ///
  text = re.sub(r'(//)([ \t\n]) ?', r'///\2', text)

  # // Author:-> /** \author */
  text = re.sub(r'(//[ ]+Author:?[ ]*)([^\n]+)', r'/** \\author \2 */', text)
  print text

if __name__ == '__main__':
  f = open(sys.argv[1], 'r')
  src = f.read()
  f.close()
  Doxygenate(src)
