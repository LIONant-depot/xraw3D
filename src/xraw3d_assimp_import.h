namespace xraw3d::assimp
{
    void ImportAll                  ( anim& Anim, geom& Geom, const char* pFileName );
    void ImportGeometryOnly         ( geom& Geom, const char* pFileName );
    void ImportGeometryAndSkeleton  ( geom& Geom, const char* pFileName );
    void ImportAnimation            ( anim& Anim, const char* pFileName );
    void ImportSkeleton             ( geom& Geom, const char* pFileName );
}