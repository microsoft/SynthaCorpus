#! /usr/bin/perl -w

# Run in a directory containing real_v_mimic PDF files.  Produce a LaTeX
# file to combine them all, one per page.

die "Can't write to figs.tex" unless open T, ">figs.tex";
print T "\\documentclass{article}
\\usepackage{graphicx}

\\begin{document}
\\title{Real versus Mimic plots}
\\author{F. Nurke}

\\maketitle{}

";

foreach $type ("unigrams", "bigrams", "cooccurs", "repetitions", 
    "distinct_terms") {

    $stype = $type;
    $stype =~ s/_/\\_/;
print  T "\\section{Plots for $stype}
";

foreach $file (glob "*_real_v_mimic_$type.pdf") {
    next if $file =~ /AS_top500k/;
    $corpus = $file;
    $corpus =~ s/_.*//;
    print T "
\\begin{figure}[h]
\\centering
\\includegraphics[width=\\textwidth]{$file}
\\caption{$stype  $corpus}
\\end{figure}

\\clearpage{}
";
}
}

print T "\\end{document}\n";

close(T);

system("pdflatex figs\n");

system("/cygdrive/c/Program\\ Files\\ \\(x86\\)//Adobe/Acrobat\\ Reader\\ DC/Reader/AcroRd32.exe figs.pdf");
