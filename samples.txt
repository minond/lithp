(def {fun}
  (\ {args body}
    {def (head args)
      (\ (tail args)
        body)}))

(fun {add-together x y}
  {+ x y})

; (print (add-together 1 2))

(fun {unpack f xs}
  {eval (join (list f) xs)})

(fun {pack f & xs}
  {f xs})

(def {uncurry} pack)
(def {curry} unpack)

(fun {len l}
  {if (== l {})
    {0}
    {+ 1 (len (tail l))}})

(fun {reverse l}
  {if (== l {})
    {{}}
    {join (reverse (tail l)) (head l)}})

; this is broken
(fun {contains l item}
  {if (== l {})
    {0}
    {if (== {item} (head l))
      {1}
      {contains (tail l) item}}})

(print {1 2 3 4 5})
(print (reverse {1 2 3 4 5}))
