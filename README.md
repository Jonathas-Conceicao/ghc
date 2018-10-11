###### Summary of the statistics gathered by readTStats ######
-----------

 Statistics | Explanation
--------------------- | ------------------
CommitPhaseTime       | Accumulated time the application spent in the commit phase (transactions that have finally committed)
CommitNumber          | Number of committed transactions
CommitReads           | Total number of transactional reads in committed transactions
CommitReadset         | Sum of the readset sizes of all committed transactions
CommitWrites          | Total number of transactional writes in committed transactions
CommitWriteset        | Sum of the writeset sizes of all committed transactions
CommitWorkTime        | Accumulated time that the application spent inside a transaction that finally committed (useful transactional work)
AbortPhaseTime        | Accumulated time the application spent in the commit phase (transactions that rolled back in the end)
AbortNumber           | Number of rolled back transactions
AbortReads            | Total number of transactional reads in rolled back transactions
AbortReadset          | Sum of the readset sizes of all rolled back transactions
AbortWrites           | Total number of transactional writes in rolled back transactions
AbortWriteset         | Sum of the writeset sizes of all rolled back transactions
AbortWorkTime         | Accumulated time that the application spent inside  a  transaction  that  finally  aborted (wasted transactional work)
HistogramOfRollbacks  | Number of transactions that were rolledback 0 .. 10 times and more than 10 time
