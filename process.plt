set xlabel "Minutes"

while (1) {
    set terminal qt 0
    set ylabel "Volt"

    plot "process.log" u ($1/60):($7==0?1/0:$7) s bezier w l t "C1", \
    "" u ($1/60):($8==0?1/0:$8) s bezier w l t "C2", \
    "" u ($1/60):($9==0?1/0:$9) s bezier w l t "C3", \
    "" u ($1/60):($10==0?1/0:$10) s bezier w l t "C4", \
    "" u ($1/60):($11==0?1/0:$11) s bezier w l t "C5", \
    "" u ($1/60):($12==0?1/0:$12) s bezier w l t "C6"

    set terminal qt 1
    set ylabel "mAh"

    plot "process.log" u ($1/60):2 s bezier w l t "mAh Charged"

    set terminal qt 2
    set ylabel "Ampere"
    plot "process.log" u ($1/60):4 w l t "Current"

    set terminal qt 3
    set ylabel "°C"
    plot "process.log" u ($1/60):($5==0?1/0:$5) w l t "External Temperature", \
    "" u ($1/60):6 w l t "Internal Temperature

    pause 2
}
