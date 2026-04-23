// Reserved for future free-function emitters. Today all outbound packet
// assembly lives as methods on HostSession / IgSession because CigiOutgoingMsg
// requires session state (version, IG-Control-first invariant) to produce
// wire-format bytes.
