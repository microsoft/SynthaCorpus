#! /usr/bin/perl -w

# Run in Experiments_emulation.  Produce a LaTeX
# file to combine them all, one per page.

die "Can't write to figsdl.tex" unless open T, ">figsdl.tex";
print T "\\documentclass{article}
\\usepackage{graphicx}

\\begin{document}
\\title{Document length modeling plots}
\\author{Hercules Griptype-Thynne}

\\maketitle{}

";

print T "\\section{Piecewise modeling of real corpora}\n";

foreach $file (glob "Lengthplots/*_dlsegs_fitting.pdf") {
    $corpus = $file;
    $corpus =~ s@Lengthplots/@@;
    $corpus =~ s/_.*//;
    print T "
\\begin{figure}[h]
\\centering
\\includegraphics[width=\\textwidth]{$file}
\\caption{$corpus}
\\end{figure}

\\clearpage{}
";
}

print T "\\section{Real v. Mimic: Gamma}\n";

foreach $file (glob "Piecewise/*_dlgamma_real_v_mimic_doclens.pdf") {
    $corpus = $file;
    $corpus =~ s/_.*//;
    print T "
\\begin{figure}[h]
\\centering
\\includegraphics[width=\\textwidth]{$file}
\\caption{$corpus}
\\end{figure}

\\clearpage{}
";
}



print T "\\section{Real v. Mimic: Adaptive Piecewise}\n";

foreach $file (glob "Piecewise/*_dlsegs_real_v_mimic_doclens.pdf") {
    $corpus = $file;
    $corpus =~ s/_.*//;
    print T "
\\begin{figure}[h]
\\centering
\\includegraphics[width=\\textwidth]{$file}
\\caption{$corpus}
\\end{figure}

\\clearpage{}
";
}


print T "\\section{Real v. Mimic: Normal}\n";

foreach $file (glob "Piecewise/*_dlnormal_real_v_mimic_doclens.pdf") {
    $corpus = $file;
    $corpus =~ s/_.*//;
    print T "
\\begin{figure}[h]
\\centering
\\includegraphics[width=\\textwidth]{$file}
\\caption{$corpus}
\\end{figure}

\\clearpage{}
";
}



print T "\\end{document}\n";

close(T);

system("pdflatex figsdl\n");

system("/cygdrive/c/Program\\ Files\\ \\(x86\\)//Adobe/Acrobat\\ Reader\\ DC/Reader/AcroRd32.exe figsdl.pdf");
