set xlabel "Minutes"

while (1) {
    set ylabel "Volt"

    plot "system.log" u ($1/60):($3==0?1/0:$3) s bezier w l t "C1", \
    "" u ($1/60):($4==0?1/0:$4) s bezier w l t "C2", \
    "" u ($1/60):($5==0?1/0:$5) s bezier w l t "C3", \
    "" u ($1/60):($6==0?1/0:$6) s bezier w l t "C4", \
    "" u ($1/60):($7==0?1/0:$7) s bezier w l t "C5", \
    "" u ($1/60):($8==0?1/0:$8) s bezier w l t "C6"

    pause 2
}
