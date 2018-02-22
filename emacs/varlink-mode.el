(require 'generic-x)

(define-generic-mode 
  'varlink-mode                    ;; name of the mode
  '("#")                           ;; comments delimiter
  nil ;; keywords
  '(
    ("^[[:space:]]*method[[:space:]]+\\([A-Z][a-zA-Z_]+\\)" 1 'font-lock-function-name-face)
    ("^[[:space:]]*\\(method\\)[[:space:]]+[A-Z][a-zA-Z_]+" 1 'font-lock-keyword-face)
    ("^[[:space:]]*\\(type\\)[[:space:]]+[A-Z][a-zA-Z_]+" 1 'font-lock-keyword-face)
    ("^[[:space:]]*\\(error\\)[[:space:]]+[A-Z][a-zA-Z_]+" 1 'font-lock-keyword-face)
    ("^[[:space:]]*\\(interface\\)[[:space:]]+[a-z]+\\([.][a-z0-9]+\\([-][a-z0-9]+\\)*\\)+[[:space:]]*\\(#.*\\)*$" 1 'font-lock-keyword-face)
    (":[[:space:]]*\\(bool\\)\\b" 1 'font-lock-type-face)
    (":[[:space:]]*\\(int\\)\\b" 1 'font-lock-type-face)
    (":[[:space:]]*\\(string\\)\\b" 1 'font-lock-type-face)
    (":[[:space:]]*\\(float\\)\\b" 1 'font-lock-type-face)
    ("[A-Z][a-zA-Z_]+" . 'font-lock-variable-name-face)
    )
  '("\\.varlink$")                    ;; files that trigger this mode
   nil                              ;; any other functions to call
  "varlink highlighting mode"     ;; doc string
)
