module identity_bft_shadow {
  type Vote = {
    validator: int,
    height: int,
    branch: str,
  }

  type HonestChoice = {
    validator: int,
    branch: str,
  }

  pure val validators = Set(0, 1, 2, 3)
  pure val honestValidators = Set(0, 1, 2)
  pure val heights = Set(1, 2, 3)
  pure val branches = Set("A", "B")
  pure val quorum = 3

  var votes: Set[Vote]
  var honestChoices: Set[HonestChoice]
  var finalized: Set[str]

  pure def voteOf(validator: int, height: int, branch: str): Vote = {
    validator: validator,
    height: height,
    branch: branch,
  }

  pure def choiceOf(validator: int, branch: str): HonestChoice = {
    validator: validator,
    branch: branch,
  }

  def hasVote(validator: int, height: int, branch: str): bool =
    voteOf(validator, height, branch).in(votes)

  def certificate(height: int, branch: str): bool =
    validators.filter(validator => hasVote(validator, height, branch)).size() >= quorum

  action init = all {
    votes' = Set(),
    honestChoices' = Set(),
    finalized' = Set(),
  }

  action vote(validator: int, height: int, branch: str): bool = all {
    validator.in(validators),
    height.in(heights),
    branch.in(branches),
    not(hasVote(validator, height, branch)),
    not(validator.in(honestValidators)) or
      honestChoices.filter(choice => choice.validator == validator and choice.branch != branch).size() == 0,
    votes' = votes.union(Set(voteOf(validator, height, branch))),
    honestChoices' = if (validator.in(honestValidators))
      honestChoices.union(Set(choiceOf(validator, branch)))
    else
      honestChoices,
    finalized' = finalized,
  }

  action finalize(branch: str): bool = all {
    branch.in(branches),
    certificate(1, branch),
    certificate(2, branch),
    certificate(3, branch),
    votes' = votes,
    honestChoices' = honestChoices,
    finalized' = finalized.union(Set(branch)),
  }

  action stutter = all {
    votes' = votes,
    honestChoices' = honestChoices,
    finalized' = finalized,
  }

  action step = any {
    {
      nondet validator = validators.oneOf()
      nondet height = heights.oneOf()
      nondet branch = branches.oneOf()
      vote(validator, height, branch)
    },
    {
      nondet branch = branches.oneOf()
      finalize(branch)
    },
    stutter,
  }

  val noConflictingFinality =
    not("A".in(finalized) and "B".in(finalized))

  val noHonestDoubleVote =
    honestValidators.forall(validator =>
      heights.forall(height =>
        not(hasVote(validator, height, "A") and hasVote(validator, height, "B"))))
}
