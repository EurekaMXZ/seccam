use thiserror::Error;

#[derive(Debug, Error)]
pub enum DomainError {
    #[error("resource not found: {0}")]
    NotFound(String),
    #[error("storage unavailable: {0}")]
    Unavailable(String),
}
