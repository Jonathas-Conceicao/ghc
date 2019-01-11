The Glasgow Haskell Compiler
============================

This is a working _in progress_ modification of the STM Haskell primitives and RunTime support.
Changes here are aimed for better composing IO functions inside transactions, including methodologies like **Transactional Boosting**. The changed happen mainly to Execution and Storage subsystems of the RTS.  

Run:```git diff ghc-8.6.1-release Composable-IO-STM-8.6``` to see full changes.
