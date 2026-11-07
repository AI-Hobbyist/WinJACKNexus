// SPDX-License-Identifier: AGPL-3.0-only
use axum_server::accept::Accept;
use std::{future::Future, io, pin::Pin, time::Duration};
use tokio::io::{AsyncRead, AsyncWrite};
use tokio_io_timeout::TimeoutStream;

// A full application queue alone cannot close a socket blocked in the HTTP
// writer. Apply the documented write-stall limit at the actual transport.
#[derive(Clone)]
pub struct WriteTimeout<A>(pub A);

impl<I, S, A> Accept<I, S> for WriteTimeout<A>
where
    A: Accept<I, S>,
    A::Future: Send + 'static,
    A::Stream: AsyncRead + AsyncWrite + Send + 'static,
    A::Service: Send + 'static,
{
    type Stream = Pin<Box<TimeoutStream<A::Stream>>>;
    type Service = A::Service;
    type Future = Pin<Box<dyn Future<Output = io::Result<(Self::Stream, Self::Service)>> + Send>>;

    fn accept(&self, stream: I, service: S) -> Self::Future {
        let accepted = self.0.accept(stream, service);
        Box::pin(async move {
            let (stream, service) = accepted.await?;
            let mut stream = TimeoutStream::new(stream);
            stream.set_write_timeout(Some(Duration::from_secs(2)));
            Ok((Box::pin(stream), service))
        })
    }
}
