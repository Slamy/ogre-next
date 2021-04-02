#version ogre_glsl_ver_330
#extension GL_ARB_shader_viewport_layer_array : require

vulkan_layout( OGRE_POSITION )	in vec2 vertex;
vulkan_layout( OGRE_NORMAL )	in vec3 normal;

in uint drawId;

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform vec2 rsDepthRange;
	uniform mat4 worldViewProj;
	uniform mat4 vrWorldViewProj[2];
vulkan( }; )

out gl_PerVertex
{
	vec4 gl_Position;
};

vulkan_layout( location = 0 )
out block
{
	vec3 cameraDir;
} outVs;

void main()
{
	//vec2 vert;
	//vert.x = vertex.x*2;
	//vert.y = vertex.y;
	
	gl_Position.xy = (vrWorldViewProj[int( drawId & 0x01u )] * vec4( vertex.xy, 0, 1.0f )).xy;

	
	//gl_Position.xy = (worldViewProj * vec4( vertex.xy, 0, 1.0f )).xy;

	gl_Position.z = rsDepthRange.y;
	gl_Position.w = 1.0f;

	if (int( drawId & 0x01u )==0)
		gl_Position.x += 0.09f;
	else
		gl_Position.x -= 0.09f;

	
	outVs.cameraDir.xyz	= normal.xyz;

	//outVs.cameraDir.x /= 2;
	
	gl_ViewportIndex	= int( drawId & 0x01u );
}
