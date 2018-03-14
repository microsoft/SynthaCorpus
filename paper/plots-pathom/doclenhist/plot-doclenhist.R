library(dplyr)
library(fitdistrplus)
library(ggplot2)
library(reshape2)
library(scales)
library(truncdist)

source("multiplot.R")

#collections <- c("AcademicID", "clueWeb12BodiesLarge", "clueWeb12Titles", "TREC-AP", "Tweets", "Wikipedia")
collections <- c("AcademicID", "classificationPaper", #"Indri-WT10g",
                 "Top100M", "TREC-AP", "Wikipedia")
#collections <- c("AcademicID", "clueWeb12Titles", "TREC-AP", "Tweets", "Wikipedia")
#collections <- c("AcademicID", "TREC-AP")

dtruncnorm <- function(x, mean, sd) { dtrunc(x, "norm", a=0, b=Inf, mean, sd) }
ptruncnorm <- function(x, mean, sd) { ptrunc(x, "norm", a=0, b=Inf, mean, sd) }

make.label.ks <- function(modelled, observed, name) {
  ks <- ks.test(modelled, observed)
  col.name <- sprintf("%s, D(%s)=%.2f%s", name, my.format(sum(observed)), ks$statistic, ifelse(ks$p.value<.05,"*",""))
}

read.and.fit <- function(name, MAXLEN=1e4) {
  cat(name, "...\n")
  
  # read the real dist'n
  fullName <- paste0("base/", name, "_doclenhist.tsv")
  cat("Read", fullName, "\n")
  tmp <- read.delim(fullName, comment.char="#", header=F, col.names=c("length", "observed"))
  tmp$collection <- name
  
  # make sure we have everything represented, as the density fns later won't have holes
  est.at <- 1:max(tmp$length) # indexing
  tmp <- left_join(data.frame(length=est.at), tmp, by=c("length"))
  tmp$observed[is.na(tmp$observed)] <- 0

  all.obs <- rep(tmp$length, times=tmp$observed) # all observations
  stopifnot(all(all.obs > 0))
  
  # if that's too big, just choose some
  N <- length(all.obs)
  all.obs <- all.obs[runif(N) < MAXLEN/N]
  
  # try to fit with a gamma
  fitted.gamma <- fitdist(all.obs, "gamma", start=c(shape=2,scale=2))
  cat(name, "gamma: shape", fitted.gamma$estimate[["shape"]], "scale", fitted.gamma$estimate[["scale"]], "\n")
  counts.gamma <- round(
      dgamma(est.at, shape=fitted.gamma$estimate[["shape"]], scale=fitted.gamma$estimate[["scale"]]) *
      N
  )
  tmp[,make.label.ks(counts.gamma, tmp$observed, "gamma")] <- counts.gamma
  
  # try to fit with a negative binomial
  fitted.nbinom <- fitdist(all.obs, "nbinom")
  cat(name, "nbimom: size", fitted.nbinom$estimate[["size"]], "mu", fitted.nbinom$estimate[["mu"]], "\n")
  counts.nbinom <- round(
    dnbinom(est.at, size=fitted.nbinom$estimate[["size"]], mu=fitted.nbinom$estimate[["mu"]]) *
      N
  )
  tmp[,make.label.ks(counts.nbinom, tmp$observed, "negative binomial")] <- counts.nbinom

  # try to fit with a truncated normal (truncate at N \in 0..Inf)
  fitted.truncnorm <- fitdist(all.obs, "truncnorm", start=c(mean=mean(all.obs), sd=sd(all.obs)))
  cat(name, "truncnorm: mean", fitted.truncnorm$estimate[["mean"]], "sd", fitted.truncnorm$estimate[["sd"]], "\n")
  counts.truncnorm <- round(
    dtruncnorm(est.at, mean=fitted.truncnorm$estimate[["mean"]], sd=fitted.truncnorm$estimate[["sd"]]) *
      N
  )
  tmp[,make.label.ks(counts.truncnorm, tmp$observed, "truncated Gaussian")] <- counts.truncnorm
  
  # read the piecewise linear fit
  fullName <- paste0("dlsegs/", name, "_doclenhist.tsv")
  cat("Read", fullName, "\n")
  pwl <- read.delim(fullName, comment.char="#", header=F, col.names=c("length", "piecewise"))
  pwl$density <- pwl$piecewise / sum(pwl$piecewise)
  # make sure est.at is covered
  pwl$length <- pwl$length - 1 # bug in piecewise code?
  pwl <- left_join(data.frame(length=est.at), pwl, by=c("length"))
  pwl$density[is.na(pwl$density)] <- 0
  # scale to N "observations"
  counts.pwl <- pwl$density*N
  tmp[,make.label.ks(counts.pwl, tmp$observed, "piecewise linear")] <- counts.pwl
  
  # only keep some of the tail -- this looks pretty
  max.counts <- pmax(tmp[,2],tmp[,4],tmp[,5],tmp[,6])
  cutoff <- max(max.counts) / 2000
  tail.starts.after <- max(which(max.counts>cutoff)) + 1
  trunc.at <- ceiling(max(tail.starts.after, nrow(tmp)/8))
  cat(tail.starts.after, " ", trunc.at, " ", nrow(tmp), " ", name, "\n")
  tmp <- tmp[1:trunc.at,]
  
  melt(tmp, id.vars=c("collection", "length"), variable.name="estimator", value.name="count")
}

# for each collection: read it, fit it, then smoosh these all together in a single data frame
d <- do.call(rbind, lapply(collections, read.and.fit))

# do some plots
my_theme = function() {
  theme_bw() +
  theme(
    legend.position=c(.95,.95),
    legend.justification=c("right", "top"),
    legend.title=element_blank(),
    legend.text=element_text(size=6)
#    plot.margin=unit(c(0,0,2,2), "cm")
  )
}

my.format <- function(x) {
  ifelse(x >= 1e6, paste0(floor(x/1e6), "M"),
  ifelse(x >= 1e3, paste0(floor(x/1e3), "k"),
  x))
}

plot.one <- function(d) {
  ggplot(d, aes(x=length, y=count, colour=estimator)) + geom_line() + #geom_point(size=0.2, alpha=0.5) +
    facet_wrap(~collection, scales="free") +
    labs(x="Document length", y="Frequency") +
    scale_y_continuous(labels=my.format) +
    my_theme()
}

# plot separately
plots <- lapply(collections, function(coll) {
  plot.one(filter(d, collection==coll))
})
pdf("doclen-dists.pdf", 8, 12)
multiplot(plotlist=plots, cols=sqrt(length(plots)))
dev.off()

# or all at once
plot.one(d) + theme(legend.position="none")
