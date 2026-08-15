module shadow_consensus {
  type Vote = {
    validator: int,
    height: int,
    round: int,
    branch: str,
  }

  type TimeoutVote = {
    validator: int,
    height: int,
    round: int,
  }

  type HonestChoice = {
    validator: int,
    height: int,
    round: int,
    branch: str,
  }

  pure val validators = Set(0, 1, 2, 3)
  pure val honestValidators = Set(0, 1, 2)
  pure val heights = Set(1, 2, 3)
  pure val rounds = Set(0, 1)
  pure val branches = Set("A", "B")
  pure val quorum = 3

  var votes: Set[Vote]
  var timeoutVotes: Set[TimeoutVote]
  var honestChoices: Set[HonestChoice]
  var availableData: Set[str]
  var currentRound: int
  var finalized: Set[str]

  pure def voteOf(validator: int, height: int, round: int, branch: str): Vote = {
    validator: validator,
    height: height,
    round: round,
    branch: branch,
  }

  pure def timeoutVoteOf(validator: int, height: int, round: int): TimeoutVote = {
    validator: validator,
    height: height,
    round: round,
  }

  pure def choiceOf(validator: int, height: int, round: int, branch: str): HonestChoice = {
    validator: validator,
    height: height,
    round: round,
    branch: branch,
  }

  def hasVote(validator: int, height: int, round: int, branch: str): bool =
    voteOf(validator, height, round, branch).in(votes)

  def hasTimeoutVote(validator: int, height: int, round: int): bool =
    timeoutVoteOf(validator, height, round).in(timeoutVotes)

  def certificate(height: int, branch: str): bool =
    rounds.exists(round =>
      validators.filter(validator => hasVote(validator, height, round, branch)).size() >= quorum)

  def timeoutCertificate(height: int, round: int): bool =
    validators.filter(validator => hasTimeoutVote(validator, height, round)).size() >= quorum

  action init = all {
    votes' = Set(),
    timeoutVotes' = Set(),
    honestChoices' = Set(),
    availableData' = Set(),
    currentRound' = 0,
    finalized' = Set(),
  }

  action publishData(branch: str): bool = all {
    branch.in(branches),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    availableData' = availableData.union(Set(branch)),
    currentRound' = currentRound,
    finalized' = finalized,
  }

  action vote(validator: int, height: int, round: int, branch: str): bool = all {
    validator.in(validators),
    height.in(heights),
    round == currentRound,
    branch.in(availableData),
    not(hasVote(validator, height, round, branch)),
    not(validator.in(honestValidators)) or
      honestChoices.filter(choice =>
        choice.validator == validator and choice.branch != branch).size() == 0,
    votes' = votes.union(Set(voteOf(validator, height, round, branch))),
    timeoutVotes' = timeoutVotes,
    honestChoices' = if (validator.in(honestValidators))
      honestChoices.union(Set(choiceOf(validator, height, round, branch)))
    else
      honestChoices,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
  }

  action timeout(validator: int, height: int, round: int): bool = all {
    validator.in(validators),
    height.in(heights),
    round == currentRound,
    not(hasTimeoutVote(validator, height, round)),
    votes' = votes,
    timeoutVotes' = timeoutVotes.union(Set(timeoutVoteOf(validator, height, round))),
    honestChoices' = honestChoices,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
  }

  action advanceRound(height: int, round: int): bool = all {
    height.in(heights),
    round == currentRound,
    timeoutCertificate(height, round),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    availableData' = availableData,
    currentRound' = round + 1,
    finalized' = finalized,
  }

  action finalize(branch: str): bool = all {
    branch.in(branches),
    certificate(1, branch),
    certificate(2, branch),
    certificate(3, branch),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized.union(Set(branch)),
  }

  action stutter = all {
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
  }

  action step = any {
    {
      nondet branch = branches.oneOf()
      publishData(branch)
    },
    {
      nondet validator = validators.oneOf()
      nondet height = heights.oneOf()
      nondet round = rounds.oneOf()
      nondet branch = branches.oneOf()
      vote(validator, height, round, branch)
    },
    {
      nondet validator = validators.oneOf()
      nondet height = heights.oneOf()
      nondet round = rounds.oneOf()
      timeout(validator, height, round)
    },
    {
      nondet height = heights.oneOf()
      nondet round = rounds.oneOf()
      advanceRound(height, round)
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
        rounds.forall(round =>
          not(hasVote(validator, height, round, "A")
            and hasVote(validator, height, round, "B")))))

  val noHonestBranchSwitch =
    honestValidators.forall(validator =>
      not(honestChoices.exists(choice =>
        choice.validator == validator and choice.branch == "A")
        and honestChoices.exists(choice =>
          choice.validator == validator and choice.branch == "B")))

  val voteRequiresAvailableData =
    votes.forall(vote => vote.branch.in(availableData))

  val roundChangeRequiresQuorum =
    currentRound == 0 or
      heights.exists(height => timeoutCertificate(height, currentRound - 1))
}
