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

  type ValidatorLock = {
    validator: int,
    height: int,
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
  var durableChoices: Set[HonestChoice]
  var validatorLocks: Set[ValidatorLock]
  var offlineValidators: Set[int]
  var availableData: Set[str]
  var currentRound: int
  var finalized: Set[str]
  var activeEpoch: int
  var preparedEpoch: int

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

  pure def lockOf(validator: int, height: int, branch: str): ValidatorLock = {
    validator: validator,
    height: height,
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
    durableChoices' = Set(),
    validatorLocks' = Set(),
    offlineValidators' = Set(),
    availableData' = Set(),
    currentRound' = 0,
    finalized' = Set(),
    activeEpoch' = 1,
    preparedEpoch' = 0,
  }

  action publishData(branch: str): bool = all {
    branch.in(branches),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData.union(Set(branch)),
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action vote(validator: int, height: int, round: int, branch: str): bool = all {
    validator.in(validators),
    height.in(heights),
    round == currentRound,
    branch.in(availableData),
    not(validator.in(offlineValidators)),
    not(hasVote(validator, height, round, branch)),
    not(validator.in(honestValidators)) or
      honestChoices.filter(choice =>
        choice.validator == validator and choice.height == height and
          choice.round == round and choice.branch != branch).size() == 0,
    not(validator.in(honestValidators)) or
      validatorLocks.filter(lock =>
        lock.validator == validator and lock.height >= height and lock.branch != branch).size() == 0,
    votes' = votes.union(Set(voteOf(validator, height, round, branch))),
    timeoutVotes' = timeoutVotes,
    honestChoices' = if (validator.in(honestValidators))
      honestChoices.union(Set(choiceOf(validator, height, round, branch)))
    else
      honestChoices,
    durableChoices' = if (validator.in(honestValidators))
      durableChoices.union(Set(choiceOf(validator, height, round, branch)))
    else
      durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action timeout(validator: int, height: int, round: int): bool = all {
    validator.in(validators),
    height.in(heights),
    round == currentRound,
    not(validator.in(offlineValidators)),
    not(hasTimeoutVote(validator, height, round)),
    votes' = votes,
    timeoutVotes' = timeoutVotes.union(Set(timeoutVoteOf(validator, height, round))),
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action advanceRound(height: int, round: int): bool = all {
    height.in(heights),
    round == currentRound,
    timeoutCertificate(height, round),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = round + 1,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action lockCertificate(validator: int, height: int, branch: str): bool = all {
    validator.in(honestValidators),
    height.in(heights),
    certificate(height, branch),
    not(validator.in(offlineValidators)),
    not(durableChoices.exists(choice =>
      choice.validator == validator and choice.height <= height and choice.branch != branch)),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks.union(Set(lockOf(validator, height, branch))),
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action crash(validator: int): bool = all {
    validator.in(honestValidators),
    not(validator.in(offlineValidators)),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices.filter(choice => choice.validator != validator),
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators.union(Set(validator)),
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action restart(validator: int): bool = all {
    validator.in(offlineValidators),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices.union(
      durableChoices.filter(choice => choice.validator == validator)),
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators.exclude(Set(validator)),
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action prepareNextEpoch: bool = all {
    preparedEpoch == 0,
    finalized.size() > 0,
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = activeEpoch + 2,
  }

  action activatePreparedEpoch: bool = all {
    preparedEpoch == activeEpoch + 2,
    finalized.size() > 0,
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = preparedEpoch,
    preparedEpoch' = 0,
  }

  action finalize(branch: str): bool = all {
    branch.in(branches),
    certificate(1, branch),
    certificate(2, branch),
    certificate(3, branch),
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized.union(Set(branch)),
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
  }

  action stutter = all {
    votes' = votes,
    timeoutVotes' = timeoutVotes,
    honestChoices' = honestChoices,
    durableChoices' = durableChoices,
    validatorLocks' = validatorLocks,
    offlineValidators' = offlineValidators,
    availableData' = availableData,
    currentRound' = currentRound,
    finalized' = finalized,
    activeEpoch' = activeEpoch,
    preparedEpoch' = preparedEpoch,
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
    {
      nondet validator = honestValidators.oneOf()
      nondet height = heights.oneOf()
      nondet branch = branches.oneOf()
      lockCertificate(validator, height, branch)
    },
    {
      nondet validator = honestValidators.oneOf()
      crash(validator)
    },
    {
      nondet validator = honestValidators.oneOf()
      restart(validator)
    },
    prepareNextEpoch,
    activatePreparedEpoch,
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

  val noDurableDoubleVote =
    honestValidators.forall(validator =>
      heights.forall(height =>
        rounds.forall(round =>
          not(durableChoices.exists(choice =>
            choice.validator == validator and choice.height == height and
              choice.round == round and choice.branch == "A")
            and durableChoices.exists(choice =>
              choice.validator == validator and choice.height == height and
                choice.round == round and choice.branch == "B")))))

  val restartRestoresDurableVote =
    honestValidators.forall(validator =>
      not(validator.in(offlineValidators)) implies
        durableChoices.filter(choice => choice.validator == validator)
          .forall(choice => choice.in(honestChoices)))

  val lockDoesNotConflictWithDurableVote =
    validatorLocks.forall(lock =>
      not(durableChoices.exists(choice =>
        choice.validator == lock.validator and choice.height <= lock.height and
          choice.branch != lock.branch)))

  val epochActivationIsTwoStep =
    preparedEpoch == 0 or preparedEpoch == activeEpoch + 2

  val epochActivationRequiresFinality =
    activeEpoch == 1 or finalized.size() > 0

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
